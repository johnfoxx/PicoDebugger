#include "svd_catalog.hpp"
#include "ide/project.hpp"
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>
#include <algorithm>

using namespace picodbg;
namespace {
constexpr qint64 maxSvdSize = 16 * 1024 * 1024;
struct Cache {
    QString source;
    std::shared_ptr<const std::vector<PeripheralDescription>> devices;
};
Cache& cache(Chip chip) {
    static Cache rp2040, rp2350;
    return chip == Chip::RP2040 ? rp2040 : rp2350;
}
QString chipName(Chip chip) { return chip == Chip::RP2040 ? "RP2040" : "RP2350"; }
QString filename(Chip chip) { return chipName(chip) + ".svd"; }
QString sdkRelativePath(Chip chip) {
    return "src/" + chipName(chip).toLower() + "/hardware_regs/" + filename(chip);
}
QString installedPath(Chip chip) {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("svd/" + filename(chip));
}
QString tag(const QDomElement& element, const char* name) { return element.firstChildElement(name).text().simplified(); }
QString inherit(QString value, const QString& fallback) { return value.isEmpty() ? fallback : value; }
qulonglong number(QString value, qulonglong fallback) {
    value.remove('_');
    if (value.startsWith('#')) value.replace(0, 1, "0b");
    bool ok = false;
    auto raw = value.startsWith("0b", Qt::CaseInsensitive) ? value.mid(2).toULongLong(&ok, 2) : value.toULongLong(&ok, 0);
    return ok ? raw : fallback;
}
std::string access(const QDomElement& node, const QString& inherited) {
    const auto write = tag(node, "modifiedWriteValues");
    if (write == "oneToClear") return "W1C";
    if (write == "oneToSet") return "W1S";
    if (write == "oneToToggle") return "W1T";
    if (!write.isEmpty() && write != "modify") return "SPECIAL";
    if (!tag(node, "readAction").isEmpty()) return "RC";
    const auto mode = inherit(tag(node, "access"), inherited);
    if (mode == "read-only") return "RO";
    if (mode == "write-only" || mode == "writeOnce") return "WO";
    if (mode == "read-write") return "RW";
    return "SPECIAL";
}

// Resolve the indexed instances used by CMSIS-SVD register and cluster arrays.
QStringList indices(const QDomElement& node) {
    const auto count = number(tag(node, "dim"), 0);
    if (!count) return {QString()};
    if (count > 4096) throw Error(ErrorKind::InvalidArgument, "Tableau de registres SVD trop grand");
    const auto indices = tag(node, "dimIndex");
    const auto range = indices.split('-');
    if (indices.contains(',')) return indices.split(',');
    QStringList result;
    for (qulonglong i = 0; i < count; ++i) {
        if (range.size() == 2 && range[0].size() == 1 && range[0].at(0).isLetter())
            result << QString(QChar(static_cast<ushort>(range[0].at(0).unicode() + i)));
        else
            result << QString::number(number(range[0], 0) + i);
    }
    return result;
}
QString indexedName(const QDomElement& node, const QString& index) {
    auto name = tag(node, "name");
    if (name.contains("%s")) return name.replace("%s", index);
    return name + index;
}
std::vector<RegisterField> fields(const QDomElement& node, const QString& inherited) {
    std::vector<RegisterField> result;
    const auto all = node.firstChildElement("fields");
    for (auto field = all.firstChildElement("field"); !field.isNull(); field = field.nextSiblingElement("field")) {
        auto lsb = number(tag(field, "bitOffset"), number(tag(field, "lsb"), 0));
        const auto bitRange = tag(field, "bitRange");
        auto msb = number(tag(field, "msb"), lsb);
        if (!bitRange.isEmpty()) {
            const auto parts = bitRange.mid(1, bitRange.size() - 2).split(':');
            msb = number(parts.front(), 32);
            lsb = number(parts.back(), 32);
        }
        const auto width = number(tag(field, "bitWidth"), msb >= lsb ? msb - lsb + 1 : 0);
        if (lsb >= 32 || !width || width > 32 - lsb) continue;
        std::vector<FieldMeaning> meanings;
        for (auto list = field.firstChildElement("enumeratedValues"); !list.isNull(); list = list.nextSiblingElement("enumeratedValues")) {
            for (auto item = list.firstChildElement("enumeratedValue"); !item.isNull(); item = item.nextSiblingElement("enumeratedValue")) {
                const auto value = number(tag(item, "value"), 0x100000000ull);
                if (value >= (1ull << width)) continue;
                const auto meaning = inherit(tag(item, "name"), tag(item, "description"));
                if (!meaning.isEmpty() && std::none_of(meanings.begin(), meanings.end(), [value](const FieldMeaning& m) { return m.value == value; }))
                    meanings.push_back({static_cast<uint32_t>(value), meaning.toStdString()});
            }
        }
        const auto fieldIndices = indices(field);
        const auto increment = number(tag(field, "dimIncrement"), 0);
        for (int instance = 0; instance < fieldIndices.size() && lsb + width <= 32; ++instance, lsb += increment)
            result.emplace_back(indexedName(field, fieldIndices[instance]).toStdString(), static_cast<unsigned>(lsb),
                static_cast<unsigned>(width), tag(field, "description").toStdString(), meanings, access(field, inherited));
    }
    return result;
}
void registers(const QDomElement& parent, uint64_t base, const QString& prefix, const QString& mode,
               std::vector<RegisterDescription>& result, unsigned depth = 0) {
    if (depth > 8) throw Error(ErrorKind::InvalidArgument, "Imbrication excessive de groupes de registres SVD");
    for (auto node = parent.firstChildElement(); !node.isNull(); node = node.nextSiblingElement()) {
        const auto kind = node.tagName();
        if (kind != "register" && kind != "cluster") continue;
        const auto children = indices(node);
        const auto increment = number(tag(node, "dimIncrement"), 0);
        const auto offset = number(tag(node, "addressOffset"), 0x100000000ull);
        if (offset > 0xffffffffull || increment > 0xffffffffull) continue;
        const auto permission = inherit(tag(node, "access"), mode);
        for (int i = 0; i < children.size(); ++i) {
            const auto address = base + offset + static_cast<uint64_t>(i) * increment;
            const auto name = prefix + indexedName(node, children[i]);
            if (address > 0xffffffffull || name.isEmpty()) continue;
            if (kind == "cluster") {
                registers(node, address, name + ".", permission, result, depth + 1);
                continue;
            }
            if (result.size() >= 32768) throw Error(ErrorKind::InvalidArgument, "Trop de registres dans le bloc SVD");
            auto bitFields = fields(node, permission);
            result.emplace_back(static_cast<uint32_t>(address), name.toStdString(), access(node, mode),
                tag(node, "description").toStdString(), std::move(bitFields), true);
        }
    }
}
void merge(RegisterDescription& local, RegisterDescription imported) {
    if (local.documentedOnly) {
        local = std::move(imported);
        return;
    }
    for (auto& field : imported.fields) {
        auto match = std::find_if(local.fields.begin(), local.fields.end(), [&](const RegisterField& f) {
            return f.lsb == field.lsb && f.width == field.width;
        });
        if (match != local.fields.end()) {
            match->access = field.access;
            if (match->description.empty()) match->description = field.description;
            if (match->meanings.empty()) match->meanings = field.meanings;
            else for (const auto& meaning : field.meanings) {
                if (std::none_of(match->meanings.begin(), match->meanings.end(), [&](const FieldMeaning& m) { return m.value == meaning.value; }))
                    match->meanings.push_back(meaning);
            }
        } else if (std::none_of(local.fields.begin(), local.fields.end(), [&](const RegisterField& f) {
            return f.lsb < field.lsb + field.width && field.lsb < f.lsb + f.width;
        })) {
            local.fields.push_back(std::move(field));
        }
    }
}
}

std::vector<PeripheralDescription> picosvd::parse(Chip chip, const QByteArray& contents) {
    if (contents.size() > maxSvdSize) throw Error(ErrorKind::InvalidArgument, "Fichier SVD trop grand");
    QDomDocument xml;
    QString detail;
    if (!xml.setContent(contents, &detail)) throw Error(ErrorKind::InvalidArgument, "SVD invalide · " + detail.toStdString());
    const auto root = xml.documentElement();
    if (root.tagName() != "device" || !tag(root, "name").startsWith(chipName(chip), Qt::CaseInsensitive))
        throw Error(ErrorKind::InvalidArgument, "Le SVD ne correspond pas à la puce sélectionnée");
    auto result = peripheralCatalog(chip);
    QMap<QString, QDomElement> blocks;
    const auto all = root.firstChildElement("peripherals");
    for (auto block = all.firstChildElement("peripheral"); !block.isNull(); block = block.nextSiblingElement("peripheral"))
        blocks.insert(tag(block, "name"), block);
    size_t imported = 0;
    for (auto pos = blocks.cbegin(); pos != blocks.cend(); ++pos) {
        const auto address = number(tag(pos.value(), "baseAddress"), 0x100000000ull);
        if (address > 0xffffffffull) continue;
        auto node = pos.value();
        auto list = node.firstChildElement("registers");
        auto permission = tag(node, "access");
        for (unsigned depth = 0; list.isNull() && node.hasAttribute("derivedFrom") && depth < 16; ++depth) {
            node = blocks.value(node.attribute("derivedFrom"));
            list = node.firstChildElement("registers");
            permission = inherit(permission, tag(node, "access"));
        }
        if (list.isNull()) continue;
        std::vector<RegisterDescription> entries;
        registers(list, address, {}, inherit(permission, inherit(tag(root, "access"), "read-write")), entries);
        if (entries.empty()) continue;
        imported += entries.size();
        auto device = std::find_if(result.begin(), result.end(), [address](const PeripheralDescription& d) { return d.base == address; });
        if (device == result.end()) {
            result.push_back({static_cast<uint32_t>(address), pos.key().toStdString(), "Non émulé", tag(node, "description").toStdString(), std::move(entries)});
            continue;
        }
        for (auto& reg : entries) {
            auto local = std::find_if(device->registers.begin(), device->registers.end(), [&](const RegisterDescription& r) { return r.address == reg.address; });
            if (local == device->registers.end()) device->registers.push_back(std::move(reg));
            else merge(*local, std::move(reg));
        }
        std::stable_sort(device->registers.begin(), device->registers.end(), [](const RegisterDescription& a, const RegisterDescription& b) { return a.address < b.address; });
    }
    if (!imported) throw Error(ErrorKind::InvalidArgument, "Aucun registre trouvé dans le fichier SVD");
    std::sort(result.begin(), result.end(), [](const PeripheralDescription& a, const PeripheralDescription& b) { return a.name < b.name; });
    return result;
}

std::shared_ptr<const std::vector<PeripheralDescription>> picosvd::catalogSnapshot(Chip chip) {
    auto& entry = cache(chip);
    if (entry.devices) return entry.devices;
    const auto sdk = picoide::Tools::detect().sdk;
    QStringList paths = {installedPath(chip)};
    if (!sdk.isEmpty()) paths << QDir(sdk).filePath(sdkRelativePath(chip));
    for (const auto& path : paths) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.size() > maxSvdSize) continue;
        try {
            entry.devices = std::make_shared<const std::vector<PeripheralDescription>>(parse(chip, file.readAll()));
            entry.source = path;
            return entry.devices;
        } catch (const Error&) {
            continue;
        }
    }
    entry.devices = std::make_shared<const std::vector<PeripheralDescription>>(peripheralCatalog(chip));
    return entry.devices;
}
const std::vector<PeripheralDescription>& picosvd::catalog(Chip chip) { return *catalogSnapshot(chip); }
QString picosvd::source(Chip chip) {
    catalog(chip);
    return cache(chip).source;
}
QString picosvd::officialUrl(Chip chip) {
    return "https://raw.githubusercontent.com/raspberrypi/pico-sdk/2.2.0/" + sdkRelativePath(chip);
}
void picosvd::install(Chip chip, const QByteArray& contents) {
    auto devices = parse(chip, contents);
    const auto path = installedPath(chip);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size() || !file.commit())
        throw Error(ErrorKind::InvalidArgument, "Enregistrement du catalogue SVD impossible · " + file.errorString().toStdString());
    auto& entry = cache(chip);
    entry.source = path;
    entry.devices = std::make_shared<const std::vector<PeripheralDescription>>(std::move(devices));
}
