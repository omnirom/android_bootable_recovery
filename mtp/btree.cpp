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

#include <iostream>
#include <utils/threads.h>
#include "btree.hpp"
#include "MtpDebug.h"

// Constructor
Tree::Tree() {
	root = NULL;
	count = 0;
}

// Destructor
Tree::~Tree() {
	freeNode(root);
}

// Free the node
void Tree::freeNode(Node* leaf)
{
	if ( leaf != NULL )
	{
		freeNode(leaf->Left());
		freeNode(leaf->Right());
		delete leaf;
	}
}

int Tree::getCount(void) {
	MTPD("node count: %d\n", count);
	return count;
}

Node* Tree::addNode(int mtpid, std::string path)
{
	MTPD("root: %d\n", root);
	// No elements. Add the root
	if ( root == NULL ) {
		Node* n = new Node();
		count++;
		MTPD("node count: %d\n", count);
		MTPD("adding node address: %d\n", n);
		MTPD("adding mtpid: %d\n", mtpid);
		n->setMtpid(mtpid);
		n->setPath(path);
		root = n;
		MTPD("set root to %d\n", root);
		return n;
	}
	else {
		count++;
		MTPD("node count: %d\n", count);
		MTPD("adding new child node\n");
		return addNode(mtpid, root, path);
	}
}

// Add a node (private)
Node* Tree::addNode(int mtpid, Node* leaf, std::string path) {
	Node* n;
	if ( mtpid <= leaf->Mtpid() )
	{
		if ( leaf->Left() != NULL )
			return addNode(mtpid, leaf->Left(), path);
		else {
			n = new Node();
			MTPD("adding mtpid: %d node: %d\n", mtpid, n);
			n->setMtpid(mtpid);
			n->setPath(path);
			n->setParent(leaf);
			leaf->setLeft(n);
		}
	}
	else
	{
		if ( leaf->Right() != NULL )
			return addNode(mtpid, leaf->Right(), path);
		else {
			n = new Node();
			MTPD("adding mtpid: %d node: %d\n", mtpid, n);
			n->setMtpid(mtpid);
			n->setPath(path);
			n->setParent(leaf);
			leaf->setRight(n);
		}
	}
	return n;
}

void Tree::setMtpParentId(int mtpparentid, Node* node) {
	node->setMtpParentId(mtpparentid);
}

std::string Tree::getPath(Node* node) {
	return node->getPath();
}

int Tree::getMtpParentId(Node* node) {
	return node->getMtpParentId();
}

Node* Tree::findNodePath(std::string path, Node* node) {
	Node* n;
	if ( node == NULL ) {
		return NULL;
	}
	if ( node->getPath().compare(path) == 0 && node->Mtpid() > 0) {
		return node;
	}
	else {
		n = findNodePath(path, node->Left());
		if (n)
			return n;
		n = findNodePath(path, node->Right());
		if (n)
			return n;
	}
	return NULL;
}

Node* Tree::getNext(Node *node) {
	if (node == NULL)
		return NULL;
	else {
		if (node->Left() != NULL)
			return node->Left();
		if (node->Right() != NULL)
			return node->Right();
	}
	return NULL;
}

Node* Tree::findNode(int key, Node* node) {
	//MTPD("key: %d\n", key);
	//MTPD("node: %d\n", node);
	if ( node == NULL ) {
		return NULL;
	}
	else if ( node->Mtpid() == key ) {
		return node;
	}
	else if ( key <= node->Mtpid() ) {
		return findNode(key, node->Left());
	}
	else if ( key > node->Mtpid() ) {
		return findNode(key, node->Right());
	}
	else {
		return NULL;
	}
	return NULL;
}

void Tree::getmtpids(Node* node, std::vector<int>* mtpids)
{
	if ( node )
	{
		MTPD("node: %d\n", node->Mtpid());
		mtpids->push_back(node->Mtpid());
		if (node->Left())
			getmtpids(node->Left(), mtpids);
		if (node->Right())
			getmtpids(node->Right(), mtpids);
	} else {
		mtpids->push_back(0);
	}
	return;
}

// Find the node with min key
// Traverse the left sub-tree recursively
// till left sub-tree is empty to get min
Node* Tree::min(Node* node)
{
	if ( node == NULL )
		return NULL;

	if ( node->Left() )
		min(node->Left());
	else
		return node;
	return NULL;
}

// Find the node with max key
// Traverse the right sub-tree recursively
// till right sub-tree is empty to get max
Node* Tree::max(Node* node)
{
	if ( node == NULL )
		return NULL;

	if ( node->Right() )
		max(node->Right());
	else
		return node;
	return NULL;
}

// Find successor to a node
// Find the node, get the node with max value
// for the right sub-tree to get the successor
Node* Tree::successor(int key, Node *node)
{
	Node* thisKey = findNode(key, node);
	if ( thisKey )
		return max(thisKey->Right());
	return NULL;
}

// Find predecessor to a node
// Find the node, get the node with max value
// for the left sub-tree to get the predecessor
Node* Tree::predecessor(int key, Node *node)
{
	Node* thisKey = findNode(key, node);
	if ( thisKey )
		return max(thisKey->Left());
	return NULL;
}

void Tree::deleteNode(int key)
{
	// Find the node.
	Node* thisKey = findNode(key, root);
	MTPD("Tree::deleteNode found node: %d\n", thisKey);
	MTPD("handle: %d\n", thisKey->Mtpid());

	if (thisKey == root) {
		if (thisKey->Right()) {
			root = thisKey->Right();
			root->setParent(NULL);
			return;
		}
		if (thisKey->Left()) {
			root = thisKey->Left();
			root->setParent(NULL);
			return;
		}
		root = NULL;
		delete thisKey;
		return;
	}

	if ( thisKey->Left() == NULL && thisKey->Right() == NULL )
	{
		if ( thisKey->Mtpid() > thisKey->Parent()->Mtpid() ) {
			thisKey->Parent()->setRight(NULL);
		}
		else {
			thisKey->Parent()->setLeft(NULL);
		}
		delete thisKey;
		return;
	}

	if ( thisKey->Left() == NULL && thisKey->Right() != NULL )
	{
		if ( thisKey->Mtpid() > thisKey->Parent()->Mtpid() )
			thisKey->Parent()->setRight(thisKey->Right());
		else
			thisKey->Parent()->setLeft(thisKey->Right());
		thisKey->Right()->setParent(thisKey->Parent());
		delete thisKey;
		return;
	}
	if ( thisKey->Left() != NULL && thisKey->Right() == NULL )
	{
		if ( thisKey->Mtpid() > thisKey->Parent()->Mtpid() )
			thisKey->Parent()->setRight(thisKey->Left());
		else
			thisKey->Parent()->setLeft(thisKey->Left());
		thisKey->Left()->setParent(thisKey->Parent());
		delete thisKey;
		return;
	}

	if ( thisKey->Left() != NULL && thisKey->Right() != NULL )
	{
		Node* sub = predecessor(thisKey->Mtpid(), thisKey);
		if ( sub == NULL )
			sub = successor(thisKey->Mtpid(), thisKey);

		if ( sub->Parent()->Mtpid() <= sub->Mtpid() )
			sub->Parent()->setRight(sub->Right());
		else
			sub->Parent()->setLeft(sub->Left());

		thisKey->setMtpid(sub->Mtpid());
		delete sub;
		return;
	}
}
