#pragma once
#ifndef VINA_BUILDER_H
#define VINA_BUILDER_H

#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <iostream>

class VinaBuilder;

class VinaObject {
private:
    std::wstring name;
    std::vector<std::pair<std::wstring, std::wstring>> data; 
    std::vector<VinaObject*> nestedObjects;
    VinaObject* parent;

public:
    VinaObject(const std::wstring& objName, VinaObject* pParent = nullptr)
        : name(objName), parent(pParent) {}

    ~VinaObject() {
        for (auto obj : nestedObjects) {
            delete obj;
        }
    }

    void AddData(const std::wstring& key, const std::wstring& value) {
        data.push_back(std::make_pair(key, value));
    }

    VinaObject* AddObject(const std::wstring& objName) {
        VinaObject* newObj = new VinaObject(objName, this);
        nestedObjects.push_back(newObj);
        return newObj;
    }

    void BuildWString(std::wstringstream& ss, int depth = 0) const {
        std::wstring indent(depth * 4, L' ');
        ss << indent << name << L"{" << std::endl;

        bool first = true;


        for (const auto& kv : data) {
            if (!first) ss << L"," << std::endl;
            ss << indent << L"    " << kv.first << L" : " << kv.second;
            first = false;
        }

        for (VinaObject* obj : nestedObjects) {
            if (!first) ss << L"," << std::endl;

            std::wstringstream nestedSS;
            obj->BuildWString(nestedSS, depth + 1);
            std::wstring nestedStr = nestedSS.str();
   
            if (!nestedStr.empty() && nestedStr.back() == L'\n') {
                nestedStr.pop_back();
                if (!nestedStr.empty() && nestedStr.back() == L'\r') nestedStr.pop_back();
            }
            ss << nestedStr;
            first = false;
        }

        ss << std::endl << indent << L"}";
    }

    std::wstring GetWString() const {
        std::wstringstream ss;
        BuildWString(ss);
        return ss.str();
    }

};

class VinaBuilder {
private:
    std::vector<VinaObject*> rootObjects;

public:
    VinaBuilder() {}

    ~VinaBuilder() {
        for (auto obj : rootObjects) {
            delete obj;
        }
    }

    VinaObject* AddObject(const std::wstring& objName) {
        VinaObject* newObj = new VinaObject(objName);
        rootObjects.push_back(newObj);
        return newObj;
    }

    std::wstring GetWString() const {
        std::wstringstream ss;
        for (size_t i = 0; i < rootObjects.size(); ++i) {
            rootObjects[i]->BuildWString(ss);
            if (i < rootObjects.size() - 1) {
                ss << std::endl; 
            }
        }
        return ss.str();
    }
    std::wstring GetContent() const {
        return GetWString();
    }
    bool SaveToFile(const std::wstring& filename) const {
        std::wofstream file(filename);
        if (!file.is_open()) {
            return false;
        }

        file << GetWString();
        file.close();
        return true;
    }
};

#endif // VINA_BUILDER_H