#include <stdio.h>
#include <string>
#include <ctype.h>

extern "C" {
#include "../twcommon.h"
}

#include "multiromedify.h"
#include "../twrp-functions.hpp"
#include "../partitions.hpp"
#include "multirom.h"

#define NPOS std::string::npos

#define HACKER_IDENT_LINE "# This updater-script has been modified modified by MultiROM and isn't suitable for flashing into primary ROM.\n"

EdifyElement::EdifyElement(EdifyElementType type) : m_type(type)
{

}

EdifyElement::~EdifyElement()
{

}


EdifyValue::EdifyValue(const std::string& text) : EdifyElement(EDF_VALUE)
{
    m_text = text;
}

void EdifyValue::write(FILE *f)
{
    fputs(m_text.c_str(), f);
}


EdifyFunc::EdifyFunc(const std::string& name) : EdifyElement(EDF_FUNC)
{
    m_name = name;
    TWFunc::trim(m_name);
}

EdifyFunc::~EdifyFunc()
{
    clearArgs();
}

void EdifyFunc::clearArgs()
{
    for(std::list<EdifyElement*>::iterator itr = m_args.begin(); itr != m_args.end(); ++itr)
        delete *itr;
    m_args.clear();
}

void EdifyFunc::addArg(EdifyElement *arg)
{
    m_args.push_back(arg);
}

void EdifyFunc::write(FILE *f)
{
    fprintf(f, "%s(", m_name.c_str());
    for(std::list<EdifyElement*>::iterator itr = m_args.begin(); itr != m_args.end(); ++itr)
        (*itr)->write(f);
    fputc(')', f);
}

std::string EdifyFunc::getArgsStr() const
{
    std::string res;
    for(std::list<EdifyElement*>::const_iterator itr = m_args.begin(); itr != m_args.end(); ++itr)
    {
        if((*itr)->getType() == EDF_VALUE)
            res.append(((const EdifyValue*)(*itr))->getText());
    }
    return res;
}

int EdifyFunc::replaceOffendings(std::list<EdifyElement*> **parentList, std::list<EdifyElement*>::iterator& lastNewlineRef)
{
    int res = 0;

    if(m_name == "mount" || m_name == "unmount" || m_name == "format")
    {
        lastNewlineRef = (*parentList)->insert(++lastNewlineRef, new EdifyValue(
                std::string("# MultiROM removed function ") + m_name +
                "(" + getArgsStr() +
                std::string(") from following line.")));
        lastNewlineRef = (*parentList)->insert(++lastNewlineRef, new EdifyNewline());

        if(m_name == "format")
        {
            for(std::list<EdifyElement*>::iterator itr = m_args.begin(); itr != m_args.end(); ++itr)
            {
                if((*itr)->getType() != EDF_VALUE)
                    continue;

                if(((EdifyValue*)(*itr))->getText().find("/system") != std::string::npos)
                {
                    res |= OFF_FORMAT_SYSTEM;
                    break;
                }
            }
        }

        res |= OFF_CHANGED;
        m_name = "ui_print";
        clearArgs();
        addArg(new EdifyValue("\"\""));
        return res;
    }
    else if(m_name == "block_image_update")
    {
        res |= OFF_BLOCK_UPDATES;
    }
    else if(m_name == "run_program")
    {
        bool rem = false;

        for(std::list<EdifyElement*>::iterator itr = m_args.begin(); itr != m_args.end(); ++itr)
        {
            if((*itr)->getType() != EDF_VALUE)
                continue;

            const std::string& t = ((EdifyValue*)(*itr))->getText();
            if(t.find("mount") != std::string::npos)
            {
                rem = true;
                break;
            }
            else if(t.find("boot.img") != NPOS || t.find(MultiROM::getBootDev()) != NPOS ||
                t.find("zImage") != NPOS || t.find("bootimg") != NPOS)
            {
                rem = false;
                break;
            }
            else if(t.find("/dev/block") != NPOS)
                rem = true;
        }

        if(rem)
        {
            std::string info = "# MultiROM replaced run_program(";
            info += getArgsStr();
            info += ") with \"/sbin/true\"";
            lastNewlineRef = (*parentList)->insert(++lastNewlineRef, new EdifyValue(info));
            lastNewlineRef = (*parentList)->insert(++lastNewlineRef, new EdifyNewline());

            res |= OFF_CHANGED;
            clearArgs();
            addArg(new EdifyValue("\"/sbin/true\""));
        }
    }
    else if(m_name == "package_extract_file" && m_args.size() >= 2)
    {
        int st = 0;

        static const char * const forbidden_images[] = {
            "radio", "bootloader", "NON-HLOS.bin", "emmc_appsboot.mbn",
            "rpm.mbn", "logo.bin", "sdi.mbn", "tz.mbn", "sbl1.mbn",
            NULL
        };

        for(std::list<EdifyElement*>::iterator itr = m_args.begin(); itr != m_args.end(); ++itr)
        {
            if((*itr)->getType() != EDF_VALUE)
                continue;

            if(st == 0)
            {
                for(int i = 0; forbidden_images[i]; ++i)
                {
                    if(((EdifyValue*)(*itr))->getText().find(forbidden_images[i]) != NPOS)
                    {
                        st = 1;
                        break;
                    }
                }
            }
            else if(st == 1 && (((EdifyValue*)(*itr))->getText().find("/dev/block/") <= 1))
            {
                st = 2;
                break;
            }
        }

        if(st == 2)
        {
            lastNewlineRef = (*parentList)->insert(++lastNewlineRef, new EdifyValue(
                std::string("# MultiROM removed function ") + m_name +
                "(" + getArgsStr() +
                std::string(") from following line.")));
            lastNewlineRef = (*parentList)->insert(++lastNewlineRef, new EdifyNewline());
            res |= OFF_CHANGED;
            m_name = "ui_print";
            clearArgs();
            addArg(new EdifyValue("\"\""));
        }
    }

    for(std::list<EdifyElement*>::iterator itr = m_args.begin(); itr != m_args.end(); ++itr)
    {
        if((*itr)->getType() == EDF_FUNC)
            res |= ((EdifyFunc*)(*itr))->replaceOffendings(parentList, lastNewlineRef);
        else if((*itr)->getType() == EDF_NEWLINE)
        {
            *parentList = &m_args;
            lastNewlineRef = itr;
        }
    }
    return res;
}


EdifyNewline::EdifyNewline() : EdifyElement(EDF_NEWLINE)
{

}

void EdifyNewline::write(FILE *f)
{
    fputc('\n', f);
}


EdifyHacker::EdifyHacker()
{

}

EdifyHacker::~EdifyHacker()
{
    while(restoreState());
    clear();
}

void EdifyHacker::clear()
{
    for(std::list<EdifyElement*>::iterator itr = m_elements.begin(); itr != m_elements.end(); ++itr)
        delete *itr;

    while(!m_openFuncs.empty())
    {
        delete m_openFuncs.top();
        m_openFuncs.pop();
    }

    while(!m_state.empty())
        m_state.pop();

    m_elements.clear();
    m_buf.clear();
    m_state.push(ST_INIT);
    m_strEscaped = false;
    m_whitespace = false;
    m_processFlags = 0;
}

void EdifyHacker::addElement(EdifyElement *el)
{
    if(m_state.top() == ST_FUNCARGS)
        m_openFuncs.top()->addArg(el);
    else
        m_elements.push_back(el);
}

void EdifyHacker::addBufAsValue()
{
    if(!m_buf.empty())
    {
        addElement(new EdifyValue(m_buf));
        m_buf.clear();
    }
}

bool EdifyHacker::add(char c)
{
    switch(m_state.top())
    {
        case ST_INIT:
        case ST_FUNCARGS:
        {
            if(isspace(c))
            {
                if(c == '\n')
                {
                    addBufAsValue();
                    addElement(new EdifyNewline());
                    m_whitespace = false;
                    break;
                }

                if(!m_whitespace)
                {
                    addBufAsValue();
                    m_whitespace = true;
                }
                m_buf += c;
                break;
            }
            else if(m_whitespace)
            {
                addBufAsValue();
                m_whitespace = false;
            }

            switch(c) {
                case '(':
                    m_openFuncs.push(new EdifyFunc(m_buf));
                    m_buf.clear();
                    m_state.push(ST_FUNCARGS);
                    break;
                case ')':
                {
                    addBufAsValue();
                    if(m_openFuncs.empty())
                    {
                        LOGERR("EdifyHacker: malformed script: invalid ')' encountered!\n");
                        return false;
                    }
                    m_state.pop();
                    EdifyFunc *f = m_openFuncs.top();
                    m_openFuncs.pop();
                    addElement(f);
                    break;
                }
                case '"':
                    addBufAsValue();
                    m_buf = c;
                    m_state.push(ST_STRING);
                    break;
                case '#':
                    addBufAsValue();
                    m_buf = c;
                    m_state.push(ST_COMMENT);
                    break;
                case '=':
                case '|':
                case '&':
                    if(m_buf.empty() || m_buf[m_buf.size()-1] != c)
                    {
                        m_buf += c;
                        break;
                    }
                    m_buf.erase(m_buf.size()-1);
                    // fallthrough
                case '+':
                case '-':
                case ',':
                case ';':
                    addBufAsValue();
                    m_buf += c;
                    if(c == '=' || c == '|' || c == '&')
                        m_buf += c;
                    addBufAsValue();
                    break;
                default:
                    m_buf += c;
                    break;
            }
            break;
        }
        case ST_STRING:
        {
            size_t len = m_buf.size();
            m_buf += c;
            if(c == '"' && !m_strEscaped)
            {
                m_state.pop();
                addBufAsValue();
            }
            else if(c == '\\')
                m_strEscaped = !m_strEscaped;
            else if(m_strEscaped)
                m_strEscaped = false;
            break;
        }
        case ST_COMMENT:
        {
            m_buf += c;
            if(c == '\n')
            {
                m_state.pop();
                addBufAsValue();
            }
            break;
        }
    }
    return true;
}

void EdifyHacker::applyOffendingMask(std::list<EdifyElement*>::iterator& itr, int mask)
{
    TWPartition *sys = NULL;

    if(mask & OFF_FORMAT_SYSTEM)
    {
        itr = m_elements.insert(++itr, new EdifyValue("# Following three lines were added by MultiROM\n"
            "run_program(\"/sbin/sh\", \"-c\", \"grep -q '/system' /etc/mtab || mount /system\");\n"
            "run_program(\"/sbin/sh\", \"-c\", \"chattr -R -i /system/*\");\n"
            "run_program(\"/sbin/sh\", \"-c\", \"rm -rf /system/*\");"));
        itr = m_elements.insert(++itr, new EdifyNewline());
    }

    if((mask & OFF_BLOCK_UPDATES) && (sys = PartitionManager.Find_Original_Partition_By_Path("/system")))
    {
        m_processFlags |= (EDIFY_BLOCK_UPDATES | EDIFY_CHANGED);
        itr = m_elements.insert(++itr, new EdifyValue("# Following line was added by MultiROM\n"
            "run_program(\"/sbin/sh\", \"-c\", \""
            "mkdir -p /tmpsystem && mount -t ext4 $(readlink -f -n "));
        itr = m_elements.insert(++itr, new EdifyValue(sys->Actual_Block_Device));
        itr = m_elements.insert(++itr, new EdifyValue(") /tmpsystem && "
            "(chattr -R -i /system/* || true) && (rm -rf /system/* || true) && "
            "(cp -a /tmpsystem/* /system/ || true) && cp_xattrs /tmpsystem /system"
            "\");"));
        itr = m_elements.insert(++itr, new EdifyNewline());
    }

    if(mask & OFF_CHANGED)
        m_processFlags |= EDIFY_CHANGED;
}

void EdifyHacker::replaceOffendings()
{
    int mask = 0;
    std::list<EdifyElement*>::iterator lastNewline = m_elements.begin();
    std::list<EdifyElement*> *parent = &m_elements;

    if(!m_elements.empty() && m_elements.front()->getType() == EDF_VALUE)
    {
        const std::string& t = ((EdifyValue*)m_elements.front())->getText();
        if(t.compare(HACKER_IDENT_LINE) == 0)
        {
            LOGINFO("EdifyHacker: this updater-script has been already processed, doing nothing.\n");
            m_processFlags = 0;
            return;
        }
    }

    for(std::list<EdifyElement*>::iterator itr = m_elements.begin(); itr != m_elements.end(); ++itr)
    {
        if((*itr)->getType() == EDF_NEWLINE)
        {
            applyOffendingMask(itr, mask);
            parent = &m_elements;
            lastNewline = itr;
            mask = 0;
        }

        if((*itr)->getType() != EDF_FUNC)
            continue;

        mask |= ((EdifyFunc*)(*itr))->replaceOffendings(&parent, lastNewline);
    }

    if(mask)
    {
        m_elements.push_back(new EdifyNewline());
        lastNewline = m_elements.end();
        applyOffendingMask(--lastNewline, mask);
    }

    if(m_processFlags & EDIFY_CHANGED)
        m_elements.push_front(new EdifyValue(HACKER_IDENT_LINE));
}

bool EdifyHacker::loadFile(const std::string& path)
{
    char buf[256];

    FILE *f = fopen(path.c_str(), "re");
    if(!f)
    {
        LOGERR("EdifyHacker: failed to open %s\n", path.c_str());
        return false;
    }

    clear();

    while(fgets(buf, sizeof(buf), f))
    {
        const int len = strlen(buf);
        for(int i = 0; i < len; ++i)
        {
            if(!add(buf[i]))
            {
                fclose(f);
                return false;
            }
        }
    }

    fclose(f);
    return true;
}

bool EdifyHacker::loadBuffer(const char *buf, size_t len)
{
    clear();
    for(size_t i = 0; i < len; ++i)
    {
        if(!add(buf[i]))
            return false;
    }
    return true;
}

bool EdifyHacker::writeToFile(const std::string& path)
{
    FILE *f = fopen(path.c_str(), "we");
    if(!f)
    {
        LOGERR("EdifyHacker: failed to open %s for writing\n", path.c_str());
        return false;
    }

    for(std::list<EdifyElement*>::iterator itr = m_elements.begin(); itr != m_elements.end(); ++itr)
        (*itr)->write(f);
    fclose(f);
    return true;
}

void EdifyHacker::saveState()
{
    std::list<EdifyElement*> state;
    copyElements(&m_elements, &state);
    m_savedStates.push_back(state);
}

bool EdifyHacker::restoreState()
{
    if(m_savedStates.empty())
        return false;

    clear();

    m_elements.swap(m_savedStates.back());
    m_savedStates.pop_back();
    return true;
}

void EdifyHacker::copyElements(std::list<EdifyElement*> *src, std::list<EdifyElement*> *dst)
{
    for(std::list<EdifyElement*>::const_iterator itr = src->begin(); itr != src->end(); ++itr)
    {
        switch((*itr)->getType())
        {
            case EDF_VALUE:
                dst->push_back(new EdifyValue(((EdifyValue*)(*itr))->getText()));
                break;
            case EDF_NEWLINE:
                dst->push_back(new EdifyNewline());
                break;
            case EDF_FUNC:
            {
                EdifyFunc *src_f = (EdifyFunc*)(*itr);
                EdifyFunc *dst_f = new EdifyFunc(src_f->getName());
                copyElements(src_f->getArgs(), dst_f->getArgs());
                dst->push_back(dst_f);
                break;
            }
        }
    }
}
