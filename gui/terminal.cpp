/*
	Copyright 2016 _that/TeamWin
	This file is part of TWRP/TeamWin Recovery Project.

	TWRP is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	TWRP is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with TWRP.  If not, see <http://www.gnu.org/licenses/>.
*/

// terminal.cpp - GUITerminal object

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <termio.h>

#include <string>
#include <cctype>
#include <linux/input.h>
#include <sys/wait.h>

extern "C" {
#include "../twcommon.h"
}
#include "../minuitwrp/minui.h"

#include "rapidxml.hpp"
#include "objects.hpp"

#if 0
#define debug_printf printf
#else
#define debug_printf(...)
#endif

extern int g_pty_fd; // in gui.cpp where the select is

/*
Pseudoterminal handler.
*/
class Pseudoterminal
{
public:
	Pseudoterminal() : fdMaster(0), pid(0)
	{
	}

	bool started() const { return pid > 0; }

	bool start()
	{
		fdMaster = getpt();
		if (fdMaster < 0) {
			LOGERR("Error %d on getpt()\n", errno);
			return false;
		}

		if (unlockpt(fdMaster) != 0) {
			LOGERR("Error %d on unlockpt()\n", errno);
			return false;
		}

		pid = fork();
		if (pid < 0) {
			LOGERR("fork failed for pty, error %d\n", errno);
			close(fdMaster);
			pid = 0;
			return false;
		}
		else if (pid) {
			// child started, now someone needs to periodically read from fdMaster
			// and write it to the terminal
			// this currently works through gui.cpp calling terminal_pty_read below
			g_pty_fd = fdMaster;
			return true;
		}
		else {
			int fdSlave = open(ptsname(fdMaster), O_RDWR);
			close(fdMaster);
			runSlave(fdSlave);
		}
		// we can't get here
		LOGERR("impossible error in pty\n");
		return false;
	}

	void runSlave(int fdSlave)
	{
		dup2(fdSlave, 0); // PTY becomes standard input (0)
		dup2(fdSlave, 1); // PTY becomes standard output (1)
		dup2(fdSlave, 2); // PTY becomes standard error (2)

		// Now the original file descriptor is useless
		close(fdSlave);

		// Make the current process a new session leader
		if (setsid() == (pid_t)-1)
			LOGERR("setsid failed: %d\n", errno);

		// As the child is a session leader, set the controlling terminal to be the slave side of the PTY
		// (Mandatory for programs like the shell to make them manage correctly their outputs)
		ioctl(0, TIOCSCTTY, 1);

		execl("/sbin/sh", "sh", NULL);
		_exit(127);
	}

	int read(char* buffer, size_t size)
	{
		if (!started()) {
			LOGERR("someone tried to read from pty, but it was not started\n");
			return -1;
		}
		int rc = ::read(fdMaster, buffer, size);
		debug_printf("pty read: %d bytes\n", rc);
		if (rc < 0) {
			// assume child has died (usual errno when shell exits seems to be EIO == 5)
			if (errno != EIO)
				LOGERR("pty read failed: %d\n", errno);
			stop();
		}
		return rc;
	}

	int write(const char* buffer, size_t size)
	{
		if (!started()) {
			LOGERR("someone tried to write to pty, but it was not started\n");
			return -1;
		}
		int rc = ::write(fdMaster, buffer, size);
		debug_printf("pty write: %d bytes -> %d\n", size, rc);
		if (rc < 0) {
			LOGERR("pty write failed: %d\n", errno);
			// assume child has died
			stop();
		}
		return rc;
	}

	template<size_t n>
	inline int write(const char (&literal)[n])
	{
		return write(literal, n-1);
	}

	void resize(int xChars, int yChars, int w, int h)
	{
		struct winsize ws;
		ws.ws_row = yChars;
		ws.ws_col = xChars;
		ws.ws_xpixel = w;
		ws.ws_ypixel = h;
		if (ioctl(fdMaster, TIOCSWINSZ, &ws) < 0)
			LOGERR("failed to set window size, error %d\n", errno);
	}

	void stop()
	{
		if (!started()) {
			LOGERR("someone tried to stop pty, but it was not started\n");
			return;
		}
		close(fdMaster);
		g_pty_fd = fdMaster = -1;
		int status;
		waitpid(pid, &status, WNOHANG); // avoid zombies but don't hang if the child is still alive and we got here due to some error
		pid = 0;
	}

private:
	int fdMaster;
	pid_t pid;
};

// UTF-8 decoder
// Copyright (c) 2008-2009 Bjoern Hoehrmann <bjoern@hoehrmann.de>
// See http://bjoern.hoehrmann.de/utf-8/decoder/dfa/ for details.

const uint32_t UTF8_ACCEPT = 0;
const uint32_t UTF8_REJECT = 1;

static const uint8_t utf8d[] = {
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 00..1f
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 20..3f
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 40..5f
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 60..7f
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9, // 80..9f
	7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7, // a0..bf
	8,8,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, // c0..df
	0xa,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x4,0x3,0x3, // e0..ef
	0xb,0x6,0x6,0x6,0x5,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8, // f0..ff
	0x0,0x1,0x2,0x3,0x5,0x8,0x7,0x1,0x1,0x1,0x4,0x6,0x1,0x1,0x1,0x1, // s0..s0
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,0,1,0,1,1,1,1,1,1, // s1..s2
	1,2,1,1,1,1,1,2,1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,2,1,1,1,1,1,1,1,1, // s3..s4
	1,2,1,1,1,1,1,1,1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,3,1,3,1,1,1,1,1,1, // s5..s6
	1,3,1,1,1,1,1,3,1,3,1,1,1,1,1,1,1,3,1,1,1,1,1,1,1,1,1,1,1,1,1,1, // s7..s8
};

uint32_t inline utf8decode(uint32_t* state, uint32_t* codep, uint32_t byte)
{
	uint32_t type = utf8d[byte];

	*codep = (*state != UTF8_ACCEPT) ?
		(byte & 0x3fu) | (*codep << 6) :
		(0xff >> type) & (byte);

	*state = utf8d[256 + *state*16 + type];
	return *state;
}
// end of UTF-8 decoder

// Append a UTF-8 codepoint to string s
size_t utf8add(std::string& s, uint32_t cp)
{
	if (cp < 0x7f) {
		s += cp;
		return 1;
	}
	else if (cp < 0x7ff) {
		s += (0xc0 | (cp >> 6));
		s += (0x80 | (cp & 0x3f));
		return 2;
	}
	else if (cp < 0xffff) {
		s += (0xe0 | (cp >> 12));
		s += (0x80 | ((cp >> 6) & 0x3f));
		s += (0x80 | (cp & 0x3f));
		return 3;
	}
	else if (cp < 0x1fffff) {
		s += (0xf0 | (cp >> 18));
		s += (0x80 | ((cp >> 12) & 0x3f));
		s += (0x80 | ((cp >> 6) & 0x3f));
		s += (0x80 | (cp & 0x3f));
		return 4;
	}
	return 0;
}

/*
TerminalEngine is the terminal back-end, dealing with the text buffer and attributes
and with communicating with the pty.
It does not care about visual things like rendering, fonts, windows etc.
The idea is that 0 to n GUITerminal instances (e.g. on different pages) can connect
to one TerminalEngine to interact with the terminal, and that the TerminalEngine
survives things like page changes or even theme reloads.
*/
class TerminalEngine
{
public:
#if 0 // later
	struct Attributes
	{
		COLOR fgcolor; // TODO: what about palette?
		COLOR bgcolor;
		// could add bold, underline, blink, etc.
	};

	struct AttributeRange
	{
		size_t start; // start position inside text (in bytes)
		Attributes a;
	};
#endif
	typedef uint32_t CodePoint; // Unicode code point

	// A line of text, optimized for rendering and storage in the buffer
	struct Line
	{
		std::string text; // in UTF-8 format
//		std::vector<AttributeRange> attrs;
		Line() {}
		size_t utf8forward(size_t start) const
		{
			if (start >= text.size())
				return start;
			uint32_t u8state = 0, u8cp = 0;
			size_t i = start;
			uint32_t rc;
			do {
				rc = utf8decode(&u8state, &u8cp, (unsigned char)text[i]);
				++i;
			} while (rc != UTF8_ACCEPT && rc != UTF8_REJECT && i < text.size());
			return i;
		}

		std::string substr(size_t start, size_t n) const
		{
			size_t i = 0;
			for (; start && i < text.size(); i = utf8forward(i))
				--start;
			size_t s = i;
			for (; n && i < text.size(); i = utf8forward(i))
				--n;
			return text.substr(s, i - s);
		}
		size_t length() const
		{
			size_t n = 0;
			for (size_t i = 0; i < text.size(); i = utf8forward(i))
				++n;
			return n;
		}
	};

	// A single character cell with a Unicode code point
	struct Cell
	{
		Cell() : cp(' ') {}
		Cell(CodePoint cp) : cp(cp) {}
		CodePoint cp;
//		Attributes a;
	};

	// A line of text, optimized for editing single characters
	struct UnpackedLine
	{
		std::vector<Cell> cells;
		void eraseFrom(size_t x)
		{
			if (cells.size() > x)
				cells.erase(cells.begin() + x, cells.end());
		}

		void eraseTo(size_t x)
		{
			if (x > 0)
				cells.erase(cells.begin(), cells.begin() + x);
		}
	};

	TerminalEngine()
	{
		// the default size will be overwritten by the GUI window when the size is known
		width = 40;
		height = 10;

		clear();
		updateCounter = 0;
		state = kStateGround;
		utf8state = utf8codepoint = 0;
	}

	void setSize(int xChars, int yChars, int w, int h)
	{
		width = xChars;
		height = yChars;
		if (pty.started())
			pty.resize(width, height, w, h);
		debug_printf("setSize: %d*%d chars, %d*%d pixels\n", xChars, yChars, w, h);
	}

	void initPty()
	{
		if (!pty.started())
		{
			pty.start();
			pty.resize(width, height, 0, 0);
		}
	}

	void readPty()
	{
		char buffer[1024];
		int rc = pty.read(buffer, sizeof(buffer));
		debug_printf("readPty: %d bytes\n", rc);
		if (rc < 0)
			output("\r\nChild process exited.\r\n");	// TODO: maybe exit terminal here
		else
			for (int i = 0; i < rc; ++i)
				output(buffer[i]);
	}

	void clear()
	{
		cursorX = cursorY = 0;
		lines.clear();
		setY(0);
		unpackLine(0);
		++updateCounter;
	}

	void output(const char *buf)
	{
		for (const char* p = buf; *p; ++p)
			output(*p);
	}

	void output(const char ch)
	{
		char debug[2]; debug[0] = ch; debug[1] = 0;
		debug_printf("output: %d %s\n", (int)ch, (ch >= ' ' && ch < 127) ? debug : ch == 27 ? "esc" : "");
		if (ch < 32) {
			// always process control chars, even after incomplete UTF-8 fragments
			processC0(ch);
			if (utf8state != UTF8_ACCEPT)
			{
				debug_printf("Terminal: incomplete UTF-8 fragment before control char ignored, codepoint=%u ch=%d\n", utf8codepoint, (int)ch);
				utf8state = UTF8_ACCEPT;
			}
			return;
		}
		uint32_t rc = utf8decode(&utf8state, &utf8codepoint, (unsigned char)ch);
		if (rc == UTF8_ACCEPT)
			processCodePoint(utf8codepoint);
		else if (rc == UTF8_REJECT) {
			debug_printf("Terminal: invalid UTF-8 sequence ignored, codepoint=%u ch=%d\n", utf8codepoint, (int)ch);
			utf8state = UTF8_ACCEPT;
		}
		// else we need to read more bytes to assemble a codepoint
	}

	bool inputChar(int ch)
	{
		debug_printf("inputChar: %d\n", ch);
		if (ch == 13)
			ch = 10;
		initPty();	// reinit just in case it died before
		// encode the char as UTF-8 and send it to the pty
		std::string c;
		utf8add(c, (uint32_t)ch);
		pty.write(c.c_str(), c.size());
		return true;
	}

	bool inputKey(int key)
	{
		debug_printf("inputKey: %d\n", key);
		switch (key)
		{
			case KEY_UP: pty.write("\e[A"); break;
			case KEY_DOWN: pty.write("\e[B"); break;
			case KEY_RIGHT: pty.write("\e[C"); break;
			case KEY_LEFT: pty.write("\e[D"); break;
			case KEY_HOME: pty.write("\eOH"); break;
			case KEY_END: pty.write("\eOF"); break;
			case KEY_INSERT: pty.write("\e[2~"); break;
			case KEY_DELETE: pty.write("\e[3~"); break;
			case KEY_PAGEUP: pty.write("\e[5~"); break;
			case KEY_PAGEDOWN: pty.write("\e[6~"); break;
			// TODO: other keys
			default:
				return false;
		}
		return true;
	}

	size_t getLinesCount() const { return lines.size(); }
	const Line& getLine(size_t n) { if (unpackedY == n) packLine(); return lines[n]; }
	int getCursorX() const { return cursorX; }
	int getCursorY() const { return cursorY; }
	int getUpdateCounter() const { return updateCounter; }

	void setX(int x)
	{
		x = min(width, max(x, 0));
		cursorX = x;
		++updateCounter;
	}

	void setY(int y)
	{
		//y = min(height, max(y, 0));
		y = max(y, 0);
		cursorY = y;
		while (lines.size() <= (size_t) y)
			lines.push_back(Line());
		++updateCounter;
	}

	void up(int n = 1) { setY(cursorY - n); }
	void down(int n = 1) { setY(cursorY + n); }
	void left(int n = 1) { setX(cursorX - n); }
	void right(int n = 1) { setX(cursorX + n); }

private:
	void packLine()
	{
		std::string& s = lines[unpackedY].text;
		s.clear();
		for (size_t i = 0; i < unpackedLine.cells.size(); ++i) {
			Cell& c = unpackedLine.cells[i];
			utf8add(s, c.cp);
			// later: if attributes changed, add attributes
		}
	}

	void unpackLine(size_t y)
	{
		uint32_t u8state = 0, u8cp = 0;
		std::string& s = lines[y].text;
		unpackedLine.cells.clear();
		for(size_t i = 0; i < s.size(); ++i) {
			uint32_t rc = utf8decode(&u8state, &u8cp, (unsigned char)s[i]);
			if (rc == UTF8_ACCEPT)
				unpackedLine.cells.push_back(Cell(u8cp));
		}
		if (unpackedLine.cells.size() < (size_t)width)
			unpackedLine.cells.resize(width);
		unpackedY = y;
	}

	void ensureUnpacked(size_t y)
	{
		if (unpackedY != y)
		{
			packLine();
			unpackLine(y);
		}
	}

	void processC0(char ch)
	{
		switch (ch)
		{
			case 7: // BEL
				DataManager::Vibrate("tw_button_vibrate");
				break;
			case 8: // BS
				left();
				break;
			case 9: // HT
				// TODO: this might be totally wrong
				right();
				while (cursorX % 8 != 0 && cursorX < width)
					right();
				break;
			case 10: // LF
			case 11: // VT
			case 12: // FF
				down();
				break;
			case 13: // CR
				setX(0);
				break;
			case 24: // CAN
			case 26: // SUB
				state = kStateGround;
				ctlseq.clear();
				break;
			case 27: // ESC
				state = kStateEsc;
				ctlseq.clear();
				break;
		}
	}

	void processCodePoint(CodePoint cp)
	{
		++updateCounter;
		debug_printf("codepoint: %u\n", cp);
		if (cp == 0x9b) // CSI
		{
			state = kStateCsi;
			ctlseq.clear();
			return;
		}
		switch (state)
		{
			case kStateGround:
				processChar(cp);
				break;
			case kStateEsc:
				processEsc(cp);
				break;
			case kStateCsi:
				processControlSequence(cp);
				break;
		}
	}

	void processChar(CodePoint cp)
	{
		ensureUnpacked(cursorY);
		// extend unpackedLine if needed, write ch into cell
		if (unpackedLine.cells.size() <= (size_t)cursorX)
			unpackedLine.cells.resize(cursorX+1);
		unpackedLine.cells[cursorX].cp = cp;

		right();
		if (cursorX >= width)
		{
			// TODO: configurable line wrapping
			// TODO: don't go down immediately but only on next char?
			down();
			setX(0);
		}
		// TODO: update all GUI objects that display this terminal engine
	}

	void processEsc(CodePoint cp)
	{
		switch (cp) {
			case 'c': // TODO: Reset
				break;
			case 'D': // Line feed
				down();
				break;
			case 'E': // Newline
				setX(0);
				down();
				break;
			case '[': // CSI
				state = kStateCsi;
				ctlseq.clear();
				break;
			case ']': // TODO: OSC state
			default:
				state = kStateGround;
		}
	}

	void processControlSequence(CodePoint cp)
	{
		if (cp >= 0x40 && cp <= 0x7e) {
			ctlseq += cp;
			execControlSequence(ctlseq);
			ctlseq.clear();
			state = kStateGround;
			return;
		}
		if (isdigit(cp) || cp == ';' /* || (ch >= 0x3c && ch <= 0x3f) */) {
			ctlseq += cp;
			// state = kStateCsiParam;
			return;
		}
	}

	static int parseArg(std::string& s, int defaultvalue)
	{
		if (s.empty() || !isdigit(s[0]))
			return defaultvalue;
		int value = atoi(s.c_str());
		size_t pos = s.find(';');
		s.erase(0, pos != std::string::npos ? pos+1 : std::string::npos);
		return value;
	}

	void execControlSequence(std::string ctlseq)
	{
		// assert(!ctlseq.empty());
		if (ctlseq == "6n") {
			// CPR - cursor position report
			char answer[20];
			sprintf(answer, "\e[%d;%dR", cursorY, cursorX);
			pty.write(answer, strlen(answer));
			return;
		}
		char f = *ctlseq.rbegin();
		// if (f == '?') ... private mode
		switch (f)
		{
			// case '@': // ICH - insert character
			case 'A': // CUU - cursor up
				up(parseArg(ctlseq, 1));
				break;
			case 'B': // CUD - cursor down
			case 'e': // VPR - line position forward
				down(parseArg(ctlseq, 1));
				break;
			case 'C': // CUF - cursor right
			case 'a': // HPR - character position forward
				right(parseArg(ctlseq, 1));
				break;
			case 'D': // CUB - cursor left
				left(parseArg(ctlseq, 1));
				break;
			case 'E': // CNL - cursor next line
				down(parseArg(ctlseq, 1));
				setX(0);
				break;
			case 'F': // CPL - cursor preceding line
				up(parseArg(ctlseq, 1));
				setX(0);
				break;
			case 'G': // CHA - cursor character absolute
				setX(parseArg(ctlseq, 1)-1);
				break;
			case 'H': // CUP - cursor position
				// TODO: consider scrollback area
				setY(parseArg(ctlseq, 1)-1);
				setX(parseArg(ctlseq, 1)-1);
				break;
			case 'J': // ED - erase in page
				{
					int param = parseArg(ctlseq, 0);
					ensureUnpacked(cursorY);
					switch (param) {
						default:
						case 0:
							unpackedLine.eraseFrom(cursorX);
							if (lines.size() > (size_t)cursorY+1)
								lines.erase(lines.begin() + cursorY+1, lines.end());
							break;
						case 1:
							unpackedLine.eraseTo(cursorX);
							if (cursorY > 0) {
								lines.erase(lines.begin(), lines.begin() + cursorY-1);
								cursorY = 0;
							}
							break;
						case 2: // clear
						case 3:	// clear incl scrollback
							clear();
							break;
					}
				}
				break;
			case 'K': // EL - erase in line
				{
					int param = parseArg(ctlseq, 0);
					ensureUnpacked(cursorY);
					switch (param) {
						default:
						case 0:
							unpackedLine.eraseFrom(cursorX);
							break;
						case 1:
							unpackedLine.eraseTo(cursorX);
							break;
						case 2:
							unpackedLine.cells.clear();
							break;
					}
				}
				break;
			// case 'L': // IL - insert line

			default:
				debug_printf("unknown ctlseq: '%s'\n", ctlseq.c_str());
				break;
		}
	}

private:
	int cursorX, cursorY; // 0-based, char based. TODO: decide how to handle scrollback
	int width, height; // window size in chars
	std::vector<Line> lines; // the text buffer
	UnpackedLine unpackedLine; // current line for editing
	size_t unpackedY; // number of current line
	int updateCounter; // changes whenever terminal could require redraw

	Pseudoterminal pty;
	enum { kStateGround, kStateEsc, kStateCsi } state;

	// for accumulating a full UTF-8 character from individual bytes
	uint32_t utf8state;
	uint32_t utf8codepoint;

	// for accumulating a control sequence after receiving CSI
	std::string ctlseq;
};

// The one and only terminal engine for now
TerminalEngine gEngine;

void terminal_pty_read()
{
	gEngine.readPty();
}


GUITerminal::GUITerminal(xml_node<>* node) : GUIScrollList(node)
{
	allowSelection = false; // terminal doesn't support list item selections
	lastCondition = false;

	if (!node) {
		mRenderX = 0; mRenderY = 0; mRenderW = gr_fb_width(); mRenderH = gr_fb_height();
	}

	engine = &gEngine;
	updateCounter = 0;
}

int GUITerminal::Update(void)
{
	if(!isConditionTrue()) {
		lastCondition = false;
		return 0;
	}

	if (lastCondition == false) {
		lastCondition = true;
		// we're becoming visible, so we might need to resize the terminal content
		InitAndResize();
	}

	if (updateCounter != engine->getUpdateCounter()) {
		// try to keep the cursor in view
		SetVisibleListLocation(engine->getCursorY());
		updateCounter = engine->getUpdateCounter();
	}

	GUIScrollList::Update();

	if (mUpdate) {
		mUpdate = 0;
		if (Render() == 0)
			return 2;
	}
	return 0;
}

// NotifyTouch - Notify of a touch event
//  Return 0 on success, >0 to ignore remainder of touch, and <0 on error
int GUITerminal::NotifyTouch(TOUCH_STATE state, int x, int y)
{
	if(!isConditionTrue())
		return -1;

	// TODO: grab focus correctly
	// TODO: fix focus handling in PageManager and GUIInput
	SetInputFocus(1);
	debug_printf("Terminal: SetInputFocus\n");
	return GUIScrollList::NotifyTouch(state, x, y);
	// TODO later: allow cursor positioning by touch (simulate mouse click?)
	// http://stackoverflow.com/questions/5966903/how-to-get-mousemove-and-mouseclick-in-bash
	// will likely not work with Busybox anyway
}

int GUITerminal::NotifyKey(int key, bool down)
{
	if (!HasInputFocus)
		return 1;
	if (down)
		if (engine->inputKey(key))
			mUpdate = 1;
	return 0;
}

// character input
int GUITerminal::NotifyCharInput(int ch)
{
	if (engine->inputChar(ch))
		mUpdate = 1;
	return 0;
}

size_t GUITerminal::GetItemCount()
{
	return engine->getLinesCount();
}

void GUITerminal::RenderItem(size_t itemindex, int yPos, bool selected)
{
	const TerminalEngine::Line& line = engine->getLine(itemindex);

	gr_color(mFontColor.red, mFontColor.green, mFontColor.blue, mFontColor.alpha);
	// later: handle attributes here

	// render text
	const char* text = line.text.c_str();
	gr_textEx_scaleW(mRenderX, yPos, text, mFont->GetResource(), mRenderW, TOP_LEFT, 0);

	if (itemindex == (size_t) engine->getCursorY()) {
		// render cursor
		int cursorX = engine->getCursorX();
		std::string leftOfCursor = line.substr(0, cursorX);
		int x = gr_ttf_measureEx(leftOfCursor.c_str(), mFont->GetResource());
		// note that this single character can be a UTF-8 sequence
		std::string atCursor = (size_t)cursorX < line.length() ? line.substr(cursorX, 1) : " ";
		int w = gr_ttf_measureEx(atCursor.c_str(), mFont->GetResource());
		gr_color(mFontColor.red, mFontColor.green, mFontColor.blue, mFontColor.alpha);
		gr_fill(mRenderX + x, yPos, w, actualItemHeight);
		gr_color(mBackgroundColor.red, mBackgroundColor.green, mBackgroundColor.blue, mBackgroundColor.alpha);
		gr_textEx_scaleW(mRenderX + x, yPos, atCursor.c_str(), mFont->GetResource(), mRenderW, TOP_LEFT, 0);
	}
}

void GUITerminal::NotifySelect(size_t item_selected)
{
	// do nothing - terminal ignores selections
}

void GUITerminal::InitAndResize()
{
	// make sure the shell is started
	engine->initPty();
	// send window resize
	int charWidth = gr_ttf_measureEx("N", mFont->GetResource());
	engine->setSize(mRenderW / charWidth, GetDisplayItemCount(), mRenderW, mRenderH);
}

void GUITerminal::SetPageFocus(int inFocus)
{
	if (inFocus && isConditionTrue()) {
		// TODO: grab focus correctly, this hack grabs focus and insists that the terminal be the focus regardless of other elements
		// It's highly unlikely that there will be any other visible input elements on the page anyway...
		SetInputFocus(1);
		InitAndResize();
	}
}
