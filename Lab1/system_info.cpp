#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <sys/utsname.h>

namespace {

std::string formatBytes(unsigned long long bytes) {
    static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    std::size_t unit = 0;

    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }

    std::ostringstream result;
    result << std::fixed << std::setprecision(1) << value << ' ' << units[unit];
    return result.str();
}

std::string readCpuName() {
    std::ifstream cpuInfo("/proc/cpuinfo");
    if (!cpuInfo) {
        throw std::runtime_error("ไม่สามารถอ่าน /proc/cpuinfo ได้");
    }

    std::string line;
    while (std::getline(cpuInfo, line)) {
        const std::string key = "model name";
        const std::size_t separator = line.find(':');
        if (separator != std::string::npos) {
            const std::string field = line.substr(0, separator);
            const std::size_t keyStart = field.find_first_not_of(" \t");
            const std::size_t keyEnd = field.find_last_not_of(" \t");
            if (keyStart != std::string::npos &&
                field.substr(keyStart, keyEnd - keyStart + 1) == key) {
                const std::size_t valueStart =
                    line.find_first_not_of(" \t", separator + 1);
                return valueStart == std::string::npos
                           ? ""
                           : line.substr(valueStart);
            }
        }
    }

    throw std::runtime_error("ไม่พบชื่อ CPU ใน /proc/cpuinfo");
}

unsigned long long readMemoryValue(const std::string& key) {
    std::ifstream memoryInfo("/proc/meminfo");
    if (!memoryInfo) {
        throw std::runtime_error("ไม่สามารถอ่าน /proc/meminfo ได้");
    }

    std::string name;
    unsigned long long value;
    std::string unit;
    while (memoryInfo >> name >> value >> unit) {
        if (name == key) {
            return value * 1024; // /proc/meminfo reports values in kB.
        }
    }

    throw std::runtime_error("ไม่พบข้อมูล " + key + " ใน /proc/meminfo");
}

std::string unquote(std::string value) {
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.size() - 2);
    }
    return value;
}

std::string readOsReleaseValue(const std::string& key) {
    std::ifstream osRelease("/etc/os-release");
    if (!osRelease) {
        throw std::runtime_error("ไม่สามารถอ่าน /etc/os-release ได้");
    }

    const std::string prefix = key + "=";
    std::string line;
    while (std::getline(osRelease, line)) {
        if (line.compare(0, prefix.size(), prefix) == 0) {
            return unquote(line.substr(prefix.size()));
        }
    }

    return "";
}

std::vector<std::pair<int, std::string>> readProcesses() {
    std::vector<std::pair<int, std::string>> processes;
    std::error_code error;
    const std::filesystem::directory_iterator end;
    std::filesystem::directory_iterator entry(
        "/proc", std::filesystem::directory_options::skip_permission_denied,
        error);

    if (error) {
        throw std::runtime_error("ไม่สามารถอ่านรายการใน /proc ได้: " +
                                 error.message());
    }

    for (; entry != end; entry.increment(error)) {
        if (error) {
            error.clear();
            continue;
        }

        const std::string filename = entry->path().filename().string();
        if (filename.empty() ||
            !std::all_of(filename.begin(), filename.end(),
                         [](unsigned char ch) { return std::isdigit(ch); })) {
            continue;
        }

        const int pid = std::stoi(filename);
        std::ifstream processInfo(entry->path() / "comm");
        std::string name;
        if (processInfo && std::getline(processInfo, name)) {
            processes.emplace_back(pid, name);
        }
    }

    std::sort(processes.begin(), processes.end());
    return processes;
}

} // namespace

int main() {
    try {
        struct utsname systemInfo {};
        if (uname(&systemInfo) != 0) {
            throw std::runtime_error("ไม่สามารถอ่านข้อมูลระบบจาก uname ได้");
        }

        const std::string osName = readOsReleaseValue("PRETTY_NAME");
        const std::string osVersion = readOsReleaseValue("VERSION_ID");
        const auto diskSpace = std::filesystem::space("/");
        if (diskSpace.capacity == static_cast<unsigned long long>(-1)) {
            throw std::runtime_error("ไม่สามารถอ่านพื้นที่จัดเก็บของ / ได้");
        }

        std::cout << "ข้อมูลระบบ\n"
                  << "ชื่อ CPU: " << readCpuName() << '\n'
                  << "หน่วยความจำทั้งหมด: "
                  << formatBytes(readMemoryValue("MemTotal:")) << '\n'
                  << "หน่วยความจำที่พร้อมใช้งาน: "
                  << formatBytes(readMemoryValue("MemAvailable:")) << '\n'
                  << "ระบบปฏิบัติการ: "
                  << (osName.empty() ? systemInfo.sysname : osName) << '\n'
                  << "เวอร์ชันระบบปฏิบัติการ: "
                  << (osVersion.empty() ? systemInfo.release : osVersion)
                  << '\n'
                  << "เวอร์ชันเคอร์เนล: " << systemInfo.release << '\n'
                  << "พื้นที่จัดเก็บของ /: "
                  << formatBytes(diskSpace.capacity) << " ทั้งหมด, "
                  << formatBytes(diskSpace.free) << " ว่าง, "
                  << formatBytes(diskSpace.capacity - diskSpace.free)
                  << " ใช้ไป\n\n"
                  << "โปรเซสที่กำลังทำงาน (PID และชื่อ):\n";

        const auto processes = readProcesses();
        for (const auto& process : processes) {
            std::cout << process.first << '\t' << process.second << '\n';
        }
        std::cout << "จำนวนโปรเซส: " << processes.size() << '\n';
    } catch (const std::exception& error) {
        std::cerr << "เกิดข้อผิดพลาด: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
