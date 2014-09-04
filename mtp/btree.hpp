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

#include <iostream>
#include <vector>
#include <utils/threads.h>
#include "MtpDebug.h"

// A generic tree node class
class Node {
	int mtpid;
	int mtpparentid;
	std::string path;
	int parentID;
	Node* left;
	Node* right;
	Node* parent;

public:
	Node();
	void setMtpid(int aMtpid);
	void setPath(std::string aPath);
	void rename(std::string aPath);
	void setLeft(Node* aLeft);
	void setRight(Node* aRight);
	void setParent(Node* aParent);
	void setMtpParentId(int id);
	int Mtpid();
	int getMtpParentId();
	std::string getPath();
	Node* Left();
	Node* Right();
	Node* Parent();
	void addProperty(uint64_t property, uint64_t valueInt, std::string valueStr, int dataType);
	void updateProperty(uint64_t property, uint64_t valueInt, std::string valueStr, int dataType);
	void addProperties(int storageID, int parent_object);
	uint64_t getIntProperty(uint64_t property);
	struct mtpProperty {
		uint64_t property;
		uint64_t valueInt;
		std::string valueStr;
		int dataType;
	};
	std::vector<mtpProperty>& getMtpProps();
	std::vector<mtpProperty> mtpProp;
};

// Binary Search Tree class
class Tree {
	Node* root;
public:
	Tree();
	~Tree();
	Node* Root() {
		MTPD("root: %d\n", root);
		return root; 
	};
	Node* addNode(int mtpid, std::string path);
	void setMtpParentId(int mtpparentid, Node* node);
	Node* findNode(int key, Node* parent);
	void getmtpids(Node* node, std::vector<int>* mtpids);
	void deleteNode(int key);
	Node* min(Node* node);
	Node* max(Node* node);
	Node* successor(int key, Node* parent);
	Node* predecessor(int key, Node* parent);
	std::string getPath(Node* node);
	int getMtpParentId(Node* node);
	Node* findNodePath(std::string path, Node* node);
	Node* getNext(Node* node);
	int getCount();

private:
	Node* addNode(int mtpid, Node* leaf, std::string path);
	void freeNode(Node* leaf);
	int count;
};

#endif
