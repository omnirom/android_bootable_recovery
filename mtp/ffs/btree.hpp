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

#ifndef BTREE_HPP
#define BTREE_HPP

#include <vector>
#include <string>
#include <map>
#include "MtpTypes.h"

// A directory entry
class Node {
	MtpObjectHandle handle;
	MtpObjectHandle parent;
	std::string name;	// name only without path

public:
	Node();
	Node(MtpObjectHandle handle, MtpObjectHandle parent, const std::string& name);
	virtual ~Node() {}

	virtual bool isDir() const { return false; }

	void rename(const std::string& newName);
	MtpObjectHandle Mtpid() const;
	MtpObjectHandle getMtpParentId() const;
	const std::string& getName() const;

	void addProperty(MtpPropertyCode property, uint64_t valueInt, std::string valueStr, MtpDataType dataType);
	void updateProperty(MtpPropertyCode property, uint64_t valueInt, std::string valueStr, MtpDataType dataType);
	void addProperties(const std::string& path, int storageID);
	uint64_t getIntProperty(MtpPropertyCode property);
	struct mtpProperty {
		MtpPropertyCode property;
		MtpDataType dataType;
		uint64_t valueInt;
		std::string valueStr;
		mtpProperty() : property(0), dataType(0), valueInt(0) {}
	};
	std::vector<mtpProperty>& getMtpProps();
	std::vector<mtpProperty> mtpProp;
	const mtpProperty& getProperty(MtpPropertyCode property);
};

// A directory
class Tree : public Node {
	std::map<MtpObjectHandle, Node*> entries;
	bool alreadyRead;
public:
	Tree(MtpObjectHandle handle, MtpObjectHandle parent, const std::string& name);
	~Tree();

	virtual bool isDir() const { return true; }

	void addEntry(Node* node);
	Node* findNode(MtpObjectHandle handle);
	void getmtpids(MtpObjectHandleList* mtpids);
	void deleteNode(MtpObjectHandle handle);
	std::string getPath(Node* node);
	int getMtpParentId() { return Node::getMtpParentId(); }
	int getMtpParentId(Node* node);
	Node* findEntryByName(std::string name);
	int getCount();
	bool wasAlreadyRead() const { return alreadyRead; }
	void setAlreadyRead(bool b) { alreadyRead = b; }
};

#endif
