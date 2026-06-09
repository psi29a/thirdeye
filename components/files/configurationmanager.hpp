#ifndef COMPONENTS_FILES_CONFIGURATIONMANAGER_HPP
#define COMPONENTS_FILES_CONFIGURATIONMANAGER_HPP

#include <unordered_map>
#include <string>

#include <components/files/fixedpath.hpp>
#include <components/files/collections.hpp>

/**
 * \namespace Files
 */
namespace Files
{

/**
 * \struct ConfigurationManager
 */
struct ConfigurationManager
{
    ConfigurationManager();
    virtual ~ConfigurationManager();

    using VariablesMap = std::unordered_map<std::string, std::string>;

    void readConfiguration(VariablesMap& variables);
    void processPaths(Files::PathContainer& dataDirs);

    /**< Fixed paths */
    const std::filesystem::path& getGlobalPath() const;
    const std::filesystem::path& getUserPath() const;
    const std::filesystem::path& getLocalPath() const;

    const std::filesystem::path& getGlobalDataPath() const;
    const std::filesystem::path& getUserDataPath() const;
    const std::filesystem::path& getLocalDataPath() const;

    const std::filesystem::path& getLogPath() const;

    private:
        typedef Files::FixedPath<> FixedPathType;

        typedef const std::filesystem::path& (FixedPathType::*path_type_f)() const;
        typedef std::unordered_map<std::string, path_type_f> TokensMappingContainer;

        void loadConfig(const std::filesystem::path& path,
            VariablesMap& variables);

        void setupTokensMapping();

        FixedPathType mFixedPath;

        std::filesystem::path mLogPath;

        TokensMappingContainer mTokensMapping;
};

} /* namespace Cfg */

#endif /* COMPONENTS_FILES_CONFIGURATIONMANAGER_HPP */
