#ifndef COMPONENTS_FILES_LINUXPATH_H
#define COMPONENTS_FILES_LINUXPATH_H

#if defined(__linux__) || defined(__FreeBSD__)

#include <filesystem>

/**
 * \namespace Files
 */
namespace Files
{

/**
 * \struct LinuxPath
 */
struct LinuxPath
{
    LinuxPath(const std::string& application_name);

    /**
     * \brief Return path to the user directory.
     *
     * \return std::filesystem::path
     */
    std::filesystem::path getUserPath() const;

    /**
     * \brief Return path to the global (system) directory where game files could be placed.
     *
     * \return std::filesystem::path
     */
    std::filesystem::path getGlobalPath() const;

    /**
     * \brief Return path to the runtime configuration directory which is the
     * place where an application was started.
     *
     * \return std::filesystem::path
     */
    std::filesystem::path getLocalPath() const;

    /**
     * \brief
     *
     * \return std::filesystem::path
     */
    std::filesystem::path getGlobalDataPath() const;

    std::string mName;
};

} /* namespace Files */

#endif /* defined(__linux__) || defined(__FreeBSD__) */

#endif /* COMPONENTS_FILES_LINUXPATH_H */
