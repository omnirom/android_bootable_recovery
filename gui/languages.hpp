#ifndef LANGUAGES_HEADER_HPP
#define LANGUAGES_HEADER_HPP

class LanguageManager
{
public:
    static int LoadLanguages(std::string package);
    static std::string parse(std::string id);
};

#endif
