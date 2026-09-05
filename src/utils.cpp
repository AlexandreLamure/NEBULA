#include <utils.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>

#include <iostream>
#include <chrono>

#ifdef OS_WIN
#include <windows.h>
#endif

namespace nebula {

#ifdef OS_WIN
bool runningInDebugger() {
    return IsDebuggerPresent();
}
#endif

void breakInDebugger() {
#ifdef OS_WIN
    if(runningInDebugger()) {
        DebugBreak();
    }
#endif
#ifdef OS_LINUX
    static bool handlerSetup = false;
    if(!handlerSetup) {
        std::signal(SIGTRAP, [](int) {});
        handlerSetup = true;
    }
    std::raise(SIGTRAP);
#endif
}

// Fatal error: print message and terminate
void fatal(const char* msg, const char* file, int line) {
    std::cerr << msg;

    if(file) {
        std::cerr << " in file \""<< file << "\"";
    }
    if(line) {
        std::cerr << " at line " << line;
    }

    std::cerr << std::endl;

    breakInDebugger();
    std::terminate();
}


static const auto startTime = std::chrono::high_resolution_clock::now();

double programTime() {
    using Seconds = std::chrono::duration<double>;
    return std::chrono::duration_cast<Seconds>(std::chrono::high_resolution_clock::now() - startTime).count();
}

Result<std::string> readTextFile(const std::string& fileName) {
    if(FILE* file = std::fopen(fileName.data(), "r")) {
        DEFER(std::fclose(file));

        std::fpos_t pos = {};
        std::fgetpos(file, &pos);
        std::fseek(file, 0, SEEK_END);
        const size_t size = ftell(file);
        std::fsetpos(file, &pos);

        std::string content(size, '\0');
        const size_t read = std::fread(content.data(), 1, size, file);

        ALWAYS_ASSERT(read <= size, "Unable to read file");
        if(read != size) {
            content.resize(read);
        }

        return {true, std::move(content)};
    }

    return {false, {}};
}


bool endsWith(std::string_view str, std::string_view suffix) {
    if(str.size() < suffix.size()) {
        return false;
    }
    return str.substr(str.size() - suffix.size()) == suffix;
}

}
