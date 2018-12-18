/*
 * Copyright (C) 2014 TeamWin - bigbiff and Dees_Troy mtp database conversion to C++
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <utils/threads.h>
#include "btree.hpp"
#include "MtpDebug.h"

// Constructor
Tree::Tree(MtpObjectHandle handle, MtpObjectHandle parent, const std::string& name)
	: Node(handle, parent, name), alreadyRead(false) {
}

// Destructor
Tree::~Tree() {
	for (std::map<MtpObjectHandle, Node*>::iterator it = entries.begin(); it != entries.end(); ++it)
		delete it->second;
	entries.clear();
}

int Tree::getCount(void) {
	int count = entries.size();
	MTPD("node count: %d\n", count);
	return count;
}

void Tree::addEntry(Node* node) {
	if (node->Mtpid() == 0) {
		MTPE("Tree::addEntry: not adding node with 0 handle.\n");
		return;
	}
	if (node->Mtpid() == node->getMtpParentId()) {
		MTPE("Tree::addEntry: not adding node with handle %u == parent.\n", node->Mtpid());
		return;
	}
	entries[node->Mtpid()] = node;
}

Node* Tree::findEntryByName(std::string name) {
	for (std::map<MtpObjectHandle, Node*>::iterator it = entries.begin(); it != entries.end(); ++it)
	{
		Node* node = it->second;
		if (node->getName().compare(name) == 0 && node->Mtpid() > 0)
			return node;
	}
	return NULL;
}

Node* Tree::findNode(MtpObjectHandle handle) {
	std::map<MtpObjectHandle, Node*>::iterator it = entries.find(handle);
	if (it != entries.end())
		return it->second;
	return NULL;
}

void Tree::getmtpids(MtpObjectHandleList* mtpids) {
	for (std::map<MtpObjectHandle, Node*>::iterator it = entries.begin(); it != entries.end(); ++it)
		mtpids->push_back(it->second->Mtpid());
}

void Tree::deleteNode(MtpObjectHandle handle) {
	std::map<MtpObjectHandle, Node*>::iterator it = entries.find(handle);
	if (it != entries.end()) {
		delete it->second;
		entries.erase(it);
	}
}
