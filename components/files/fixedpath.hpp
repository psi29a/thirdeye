#ifndef COMPONENTS_FILES_FIXEDPATH_HPP
#define COMPONENTS_FILES_FIXEDPATH_HPP

#include <string>
#include <filesystem>

#if defined(__linux__) || defined(__FreeBSD__)
    #include <components/files/linuxpath.hpp>
    namespace Files { typedef LinuxPath TargetPathType; }

#elif defined(__WIN32) || defined(__WINDOWS__) || defined(_WIN32)
    #include <components/files/windowspath.hpp>
    namespace Files { typedef WindowsPath TargetPathType; }

#elif defined(macintosh) || defined(Macintosh) || defined(__APPLE__) || defined(__MACH__)
    #include <components/files/macospath.hpp>
    namespace Files { typedef MacOsPath TargetPathType; }

#else
    #error "Unknown platform!"
#endif


/**
 * \namespace Files
 */
namespace Files
{

/**
 * \struct Path
 *
 * \tparam P - Path strategy class type (depends on target system)
 *
 */
template
<
    class P = TargetPathType
>
struct FixedPath
{
    typedef P PathType;

    /**
     * \brief Path constructor.
     *
     * \param [in] application_name - Name of the application
     */
    FixedPath(const std::string& application_name)
        : mPath(application_name + "/")
        , mUserPath(mPath.getUserPath())
        , mGlobalPath(mPath.getGlobalPath())
        , mLocalPath(mPath.getLocalPath())
        , mGlobalDataPath(mPath.getGlobalDataPath())
    {
    }

    /**
     * \brief Return path pointing to the user local configuration directory.
     *
     * \return std::filesystem::path
     */
    const std::filesystem::path& getUserPath() const
    {
        return (mUserPath);
    }

    /**
     * \brief Return path pointing to the global (system) configuration directory.
     *
     * \return std::filesystem::path
     */
    const std::filesystem::path& getGlobalPath() const
    {
        return (mGlobalPath);
    }

    /**
     * \brief Return path pointing to the directory where application was started.
     *
     * \return std::filesystem::path
     */
    const std::filesystem::path& getLocalPath() const
    {
        return (mLocalPath);
    }

    const std::filesystem::path& getGlobalDataPath() const
    {
        return (mGlobalDataPath);
    }

    private:
        PathType mPath;

        std::filesystem::path mUserPath;       /**< User path  */
        std::filesystem::path mGlobalPath;     /**< Global path */
        std::filesystem::path mLocalPath;      /**< It is the same directory where application was run */
        std::filesystem::path mGlobalDataPath; /**< Global application data path */

};


} /* namespace Files */

#endif /* COMPONENTS_FILES_FIXEDPATH_HPP */
