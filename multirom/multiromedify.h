#ifndef MULTIROM_EDIFY_H
#define MULTIROM_EDIFY_H

#include <stdio.h>
#include <string>
#include <list>
#include <stack>
#include <vector>

enum EdifyElementType {
    EDF_VALUE,
    EDF_FUNC,
    EDF_NEWLINE,
};

enum
{
    // Internal flags
    OFF_FORMAT_SYSTEM = 0x01,
    OFF_BLOCK_UPDATES = 0x02,
    OFF_CHANGED       = 0x04,

    // returned by EdifyHacker::getProcessFlags()
    EDIFY_CHANGED     = 0x01,
    EDIFY_BLOCK_UPDATES = 0x02,
};

class EdifyElement
{
public:
    EdifyElement(EdifyElementType type);
    virtual ~EdifyElement();

    EdifyElementType getType() const { return m_type; }

    virtual void write(FILE *f) = 0;

protected:
    EdifyElementType m_type;
};

class EdifyValue : public EdifyElement
{
public:
    EdifyValue(const std::string& text);

    const std::string& getText() const { return m_text; }

    void write(FILE *f);
private:
    std::string m_text;
};

class EdifyFunc : public EdifyElement
{
public:
    EdifyFunc(const std::string& name);
    virtual ~EdifyFunc();

    void addArg(EdifyElement *arg);
    void write(FILE *f);
    const std::string& getName() const { return m_name; }
    std::list<EdifyElement*> *getArgs() { return &m_args; }
    const std::list<EdifyElement*> *getArgs() const { return &m_args; }
    int replaceOffendings(std::list<EdifyElement*> **parentList, std::list<EdifyElement*>::iterator& lastNewlineRef);
    std::string getArgsStr() const;

private:
    void clearArgs();

    std::string m_name;
    std::list<EdifyElement*> m_args;
};

class EdifyNewline : public EdifyElement
{
public:
    EdifyNewline();
    void write(FILE *f);
};

class EdifyHacker
{
public:
    EdifyHacker();
    ~EdifyHacker();

    bool loadFile(const std::string& path);
    bool loadBuffer(const char *buf, size_t len);
    void replaceOffendings();
    bool writeToFile(const std::string& path);
    void clear();
    void saveState();
    bool restoreState();

    int getProcessFlags() const { return m_processFlags; }

private:
    enum ParserState {
        ST_INIT,
        ST_COMMENT,
        ST_FUNCARGS,
        ST_STRING,
    };

    bool add(char c);
    void addElement(EdifyElement *el);
    void addBufAsValue();
    void applyOffendingMask(std::list<EdifyElement*>::iterator& itr, int mask);
    void copyElements(std::list<EdifyElement*> *src, std::list<EdifyElement*> *dst);
    void printStatsForState(const std::list<EdifyElement*>& state);

    std::string m_buf;
    std::stack<ParserState> m_state;
    std::stack<EdifyFunc*> m_openFuncs;
    bool m_strEscaped;
    bool m_whitespace;

    int m_processFlags;
    std::list<EdifyElement*> m_elements;
    std::vector<std::list<EdifyElement*> > m_savedStates;
};

#endif
