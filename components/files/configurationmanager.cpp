#include "configurationmanager.hpp"

#include <string>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cctype>

namespace
{

static void replace_all(std::string& str, const std::string& from, const std::string& to)
{
    if (from.empty())
        return;
    std::string::size_type start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos)
    {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length();
    }
}

static void erase_all(std::string& str, const std::string& pattern)
{
    replace_all(str, pattern, "");
}

static std::string trim(const std::string& value)
{
    auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
        return std::string();
    auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

static bool empty_or_comment(const std::string& line)
{
    auto trimmed = trim(line);
    return trimmed.empty() || trimmed[0] == '#';
}

} // namespace

/**
 * \namespace Files
 */
namespace Files
{

//static const char* const thirdeyeCfgFile = "thirdeye.cfg";

const char* const localToken = "?local?";
const char* const userToken = "?user?";
const char* const globalToken = "?global?";

ConfigurationManager::ConfigurationManager()
    : mFixedPath("thirdeye")
{
    setupTokensMapping();

    std::filesystem::create_directories(mFixedPath.getUserPath());

    mLogPath = mFixedPath.getUserPath();
}

ConfigurationManager::~ConfigurationManager()
{
}

void ConfigurationManager::setupTokensMapping()
{
    mTokensMapping.insert(std::make_pair(localToken, &FixedPath<>::getLocalPath));
    mTokensMapping.insert(std::make_pair(userToken, &FixedPath<>::getUserPath));
    mTokensMapping.insert(std::make_pair(globalToken, &FixedPath<>::getGlobalDataPath));
}

void ConfigurationManager::readConfiguration(VariablesMap& variables)
{
    loadConfig(mFixedPath.getUserPath(), variables);
    loadConfig(mFixedPath.getLocalPath(), variables);
    loadConfig(mFixedPath.getGlobalPath(), variables);
}

void ConfigurationManager::processPaths(Files::PathContainer& dataDirs)
{
    std::string path;
    for (Files::PathContainer::iterator it = dataDirs.begin(); it != dataDirs.end(); ++it)
    {
        path = it->string();
        erase_all(path, "\"");
        *it = std::filesystem::path(path);

        // Check if path contains a token
        if (!path.empty() && *path.begin() == '?')
        {
            std::string::size_type pos = path.find('?', 1);
            if (pos != std::string::npos && pos != 0)
            {
                TokensMappingContainer::iterator tokenIt = mTokensMapping.find(path.substr(0, pos + 1));
                if (tokenIt != mTokensMapping.end())
                {
                    std::filesystem::path tempPath(((mFixedPath).*(tokenIt->second))());
                    if (pos < path.length() - 1)
                    {
                        // There is something after the token, so we should
                        // append it to the path
                        tempPath /= path.substr(pos + 1, path.length() - pos);
                    }

                    *it = std::move(tempPath);
                }
                else
                {
                    // Clean invalid / unknown token, it will be removed outside the loop
                    (*it).clear();
                }
            }
        }

        if (!std::filesystem::is_directory(*it))
        {
            (*it).clear();
        }
    }

    dataDirs.erase(std::remove_if(dataDirs.begin(), dataDirs.end(),
        [](const std::filesystem::path& path) { return path.empty(); }), dataDirs.end());
}

void ConfigurationManager::loadConfig(const std::filesystem::path& path,
    VariablesMap& variables)
{
    static const std::string thirdeyeCfgFile = "thirdeye.cfg";
    std::filesystem::path cfgFile = path / thirdeyeCfgFile;
    if (std::filesystem::is_regular_file(cfgFile))
    {
        std::cout << "Loading config file: " << cfgFile.string() << "... ";

        std::ifstream configFileStream(cfgFile.string());
        if (configFileStream.is_open())
        {
            std::string line;
            while (std::getline(configFileStream, line))
            {
                if (empty_or_comment(line))
                    continue;

                auto pos = line.find('=');
                if (pos == std::string::npos)
                    continue;

                std::string key = trim(line.substr(0, pos));
                std::string value = trim(line.substr(pos + 1));
                if (!key.empty())
                {
                    variables[std::move(key)] = std::move(value);
                }
            }
            std::cout << "done." << std::endl;
        }
        else
        {
            std::cout << "failed." << std::endl;
        }
    }
}

const std::filesystem::path& ConfigurationManager::getGlobalPath() const
{
    return (mFixedPath.getGlobalPath());
}

const std::filesystem::path& ConfigurationManager::getUserPath() const
{
    return (mFixedPath.getUserPath());
}

const std::filesystem::path& ConfigurationManager::getLocalPath() const
{
    return (mFixedPath.getLocalPath());
}

const std::filesystem::path& ConfigurationManager::getGlobalDataPath() const
{
    return (mFixedPath.getGlobalDataPath());
}

const std::filesystem::path& ConfigurationManager::getLogPath() const
{
    return (mLogPath);
}

} /* namespace Cfg */
