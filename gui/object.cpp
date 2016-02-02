// object.cpp - GUIObject base class

#include <stdio.h>
#include <stdlib.h>

#include <string>

extern "C" {
#include "../twcommon.h"
#include "../variables.h"
}

#include "rapidxml.hpp"
#include "objects.hpp"
#include "../data.hpp"

GUIObject::GUIObject(xml_node<>* node)
{
	mConditionsResult = true;
	if (node)
		LoadConditions(node, mConditions);
}

void GUIObject::LoadConditions(xml_node<>* node, std::vector<Condition>& conditions)
{
	xml_node<>* condition = FindNode(node, "conditions");
	if (condition)  condition = FindNode(condition, "condition");
	else			condition = FindNode(node, "condition");

	while (condition)
	{
		Condition cond;

		cond.mCompareOp = "=";

		xml_attribute<>* attr;
		attr = condition->first_attribute("var1");
		if (attr)   cond.mVar1 = attr->value();

		attr = condition->first_attribute("op");
		if (attr)   cond.mCompareOp = attr->value();

		attr = condition->first_attribute("var2");
		if (attr)   cond.mVar2 = attr->value();

		conditions.push_back(cond);

		condition = condition->next_sibling("condition");
	}
}

GUIObject::~GUIObject()
{
}

bool GUIObject::IsConditionVariable(std::string var)
{
	std::vector<Condition>::iterator iter;
	for (iter = mConditions.begin(); iter != mConditions.end(); iter++)
	{
		if (iter->mVar1 == var)
			return true;
	}
	return false;
}

bool GUIObject::isConditionTrue()
{
	return mConditionsResult;
}

bool GUIObject::isConditionTrue(Condition* condition)
{
	// This is used to hold the proper value of "true" based on the '!' NOT flag
	bool bTrue = true;

	if (condition->mVar1.empty())
		return bTrue;

	if (!condition->mCompareOp.empty() && condition->mCompareOp[0] == '!')
		bTrue = false;

	if (condition->mVar2.empty() && condition->mCompareOp != "modified")
	{
		if (!DataManager::GetStrValue(condition->mVar1).empty())
			return bTrue;

		return !bTrue;
	}

	string var1, var2;
	if (DataManager::GetValue(condition->mVar1, var1))
		var1 = condition->mVar1;
	if (DataManager::GetValue(condition->mVar2, var2))
		var2 = condition->mVar2;

	if (var2.substr(0, 2) == "{@")
		// translate resource string in value
		var2 = gui_parse_text(var2);

	// This is a special case, we stat the file and that determines our result
	if (var1 == "fileexists")
	{
		struct stat st;
		if (stat(var2.c_str(), &st) == 0)
			var2 = var1;
		else
			var2 = "FAILED";
	}
	if (var1 == "mounted")
	{
		if (isMounted(condition->mVar2))
			var2 = var1;
		else
			var2 = "FAILED";
	}

	if (condition->mCompareOp.find('=') != string::npos && var1 == var2)
		return bTrue;

	if (condition->mCompareOp.find('>') != string::npos && (atof(var1.c_str()) > atof(var2.c_str())))
		return bTrue;

	if (condition->mCompareOp.find('<') != string::npos && (atof(var1.c_str()) < atof(var2.c_str())))
		return bTrue;

	if (condition->mCompareOp == "modified")
	{
		// This is a hack to allow areas to reset the default value
		if (var1.empty())
		{
			condition->mLastVal = var1;
			return !bTrue;
		}

		if (var1 != condition->mLastVal)
			return bTrue;
	}

	return !bTrue;
}

bool GUIObject::isConditionValid()
{
	return !mConditions.empty();
}

int GUIObject::NotifyVarChange(const std::string& varName, const std::string& value __unused)
{
	mConditionsResult = UpdateConditions(mConditions, varName);
	return 0;
}

bool GUIObject::UpdateConditions(std::vector<Condition>& conditions, const std::string& varName)
{
	bool result = true;

	const bool varNameEmpty = varName.empty();
	std::vector<Condition>::iterator iter;
	for (iter = conditions.begin(); iter != conditions.end(); ++iter)
	{
		if(varNameEmpty && iter->mCompareOp == "modified")
		{
			string val;

			// If this fails, val will not be set, which is perfect
			if (DataManager::GetValue(iter->mVar1, val))
			{
				DataManager::SetValue(iter->mVar1, "");
				DataManager::GetValue(iter->mVar1, val);
			}
			iter->mLastVal = val;
		}

		if(varNameEmpty || iter->mVar1 == varName || iter->mVar2 == varName)
			iter->mLastResult = isConditionTrue(&(*iter));

		if(!iter->mLastResult)
			result = false;
	}
	return result;
}

bool GUIObject::isMounted(string vol)
{
	FILE *fp;
	char tmpOutput[255];

	fp = fopen("/proc/mounts", "rt");
	while (fgets(tmpOutput,255,fp) != NULL)
	{
		char* mnt = tmpOutput;
		while (*mnt > 32)			mnt++;
		while (*mnt > 0 && *mnt <= 32)	mnt++;
		char* pos = mnt;
		while (*pos > 32)   pos++;
		*pos = 0;
		if (vol == mnt)
		{
			fclose(fp);
			return true;
		}
	}
	fclose(fp);
	return false;
}
