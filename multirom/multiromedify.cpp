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

bool is_word(const std::string& text_string, const char * word, const char * additional_pre_allowed = "", const char * additional_post_allowed = "")
{
    size_t pos = text_string.find(word);
    bool pre_check  = true;
    bool post_check = true;

    if (strcmp(additional_pre_allowed, "dont_check") !=0)
    {
        char pre_word_char  = text_string[pos - 1];
        pre_check  = isspace(pre_word_char)  || ( strchr(additional_pre_allowed, pre_word_char) != NULL );
    }

    if (strcmp(additional_post_allowed, "dont_check") !=0)
    {
        char post_word_char = text_string[pos + strlen(word) + 1];
        post_check = isspace(post_word_char) || ( strchr(additional_post_allowed, post_word_char) != NULL );
    }

    return pre_check && post_check;
}

int EdifyFunc::replaceOffendings(std::list<EdifyElement*> **parentList, std::list<EdifyElement*>::iterator& lastNewlineRef)
{
    int res = 0;

    if(m_name == "mount" || m_name == "unmount" || m_name == "format")
    {
        // we only care about certain partitions, disregard the rest (eg systemless root 'su')
        int found = 0;
        static const char *offending_mounts[] = {
            "/cache",
            "/system",
            "/data",
            "/userdata",
            NULL
        };

        for(std::list<EdifyElement*>::iterator itr = m_args.begin(); itr != m_args.end(); ++itr)
        {
            if((*itr)->getType() != EDF_VALUE)
                continue;

            for(int i = 0; offending_mounts[i]; ++i)
            {
                const std::string& t = ((EdifyValue*)(*itr))->getText();
                if(t.find(offending_mounts[i]) != NPOS)
                {
                    // we found one, but make sure it's not embedded such as "/data/su.img"
                    if ( is_word(t, offending_mounts[i], "dont_check", "\"") )
                    {
                        found = 1;
                        break;
                    }
                }
            }
            if (found) break;
        }

        if (!found)
            return res; // we didn't find an offending partition, so abort change


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
        addArg(new EdifyValue("\" \""));
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
                // check it's the actual mount command, and not part of something longer such as: run_program("/tmp/mount_su_image.sh");
                if ( is_word(t, "mount", "\"`/;", "\"`/;") )
                {
                    rem = true;
                    break;
                }
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

                // package_extract_file("system.img", "/dev/block/platform/msm_sdcc.1/by-name/system");
                if(st == 0 && (((EdifyValue*)(*itr))->getText().find("system.img") != NPOS))
                {
                    st = 3;
                }
            }
            else if(st == 1 && (((EdifyValue*)(*itr))->getText().find("/dev/block/") <= 1))
            {
                st = 2;
                break;
            }

            // package_extract_file("system.img", "/dev/block/platform/msm_sdcc.1/by-name/system");
            else if(st == 3 && (((EdifyValue*)(*itr))->getText().find("/dev/block/") <= 1))
            {
                st = 4;
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
            addArg(new EdifyValue("\" \""));
        }
        else if(st == 4)
        {
            res |= OFF_BLOCK_UPDATES;
        }
    }
    else if(m_name == "range_sha1")
    {
        res |= OFF_CHANGED;
        m_name = "true || " + m_name;
        lastNewlineRef = (*parentList)->insert(++lastNewlineRef, new EdifyValue(std::string("# MultiROM made if on the next line always pass")));
        lastNewlineRef = (*parentList)->insert(++lastNewlineRef, new EdifyNewline());
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


/*
 * void Move_to_End_Of_Command(std::list<EdifyElement*>::iterator& itr)
 * 
 *
 * Helper function for 'applyOffendingMask' (in particular block_image_update - OFF_BLOCK_UPDATES)
 * 
 * We don't want the shell commands to be inserted "in between" two or more consecutive consecutive. newline isn't enough, we need to wait for semicolon
 * otherwise the command will get inserted after newline but before the rest of [any possible other] commands. Example: in CM14's block_image_update:
 * 
 *     block_image_update("/dev/block/platform/msm_sdcc.1/by-name/system", package_extract_file("system.transfer.list"), "system.new.dat", "system.patch.dat") ||
 *       abort("E1001: Failed to update system image.");
 *
 * Normally the copy back from "fake system" would be inserted before EOL (after || above), instead, we want the full block_image_update(...) || abort(...); to
 * complete and then if all is good, copy the system files to MultiROM's system directory.
 *
 * I'm sure there's a better/nicer way of doing this, but it works
 */
void Move_to_End_Of_Command(std::list<EdifyElement*>::iterator& itr)
{
    bool found_semicolon = false;
    int rollback = 0;  // guess that's the easiest way to fast-forward to where we were with a std::list

    // we're currently on EDF_NEWLINE or at least should be according to the calling function... double check for that??

    // step 1: check backwards: (a) not more than EDF_FUNC and (b) EDF_VALUE == ;
    while((*itr)->getType() != EDF_FUNC)
    {
        if((*itr)->getType() == EDF_VALUE)
        {
            if(((EdifyValue*)(*itr))->getText() == ";")
            {
                found_semicolon = true;
                break;
            }
        }
        --itr;
        ++rollback;
    }

    // step 2: go back to original position
    while(rollback > 0)
    {
        ++itr;
        --rollback;
    }

    // step 3: if we haven't found a semicolon then run forward till we find one
    while(!found_semicolon)
    {
        if((*itr)->getType() == EDF_VALUE)
        {
            if(((EdifyValue*)(*itr))->getText() == ";")
            {
                found_semicolon = true;
                break;
            }
        }
        ++itr;
    }

    // step 4: if we've found the semicolon, but are not on EDF_NEWLINE, run forward till EDF_NEWLINE
    if(found_semicolon && !((*itr)->getType() == EDF_NEWLINE))
    {
        while((*itr)->getType() != EDF_NEWLINE)
        {
            ++itr;
        }
    }
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
        Move_to_End_Of_Command(itr);

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
            m_processFlags = 0; // FIXME: potential problem: if the script needs EDIFY_BLOCK_UPDATES tmpsystem.img it wont be created, in this case
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
