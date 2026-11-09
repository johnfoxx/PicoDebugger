#pragma once

// Install before QApplication so platform-plugin startup warnings are included.
// Opt-in only; ordinary runs retain Qt's default message handling.
bool installQtDiagnostics(int argc, char* const argv[]);
void reportQtEnvironment();
