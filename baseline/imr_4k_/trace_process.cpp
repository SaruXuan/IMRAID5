#include "trace_process.h"
#include <fstream>
#include <string>
#include <iostream>

std::vector<std::string> split_trace(const std::string& str, const std::string& start_char, const std::string& delim)
{
    std::vector<std::string> res;
    if ("" == str)
        return res;

    std::string strs = str + delim;
    size_t start_pos;
    size_t pos;
    size_t size = strs.size();

    start_pos = strs.find_first_of(start_char);

    for (size_t i = start_pos; i < size; i++)
    {
        pos = strs.find(delim, i); //return std::string::npos when no match, which is all 1 digits in binary way
        if (pos < size)
        {
            std::string s = strs.substr(i, pos - i);
            res.push_back(s);
            i = pos + delim.size() - 1;
        }
    }
    return res;
}

void makeTraceList(std::string fileName, std::vector<std::vector<std::string>>& trace_list)
{
    std::vector<std::string> sList;
    std::string s;

    std::ifstream fin(fileName);
    if (!fin)
    {
        std::cout << "Error when reading\n";
        return;
    }

    while (!fin.eof())
    {
        getline(fin, s);
        sList.push_back(s);
    }

    for (int i = 0; i < sList.size(); i++)
    {
        std::string start_char = "WR";
        std::string delim = ",";
        std::vector<std::string> vec = split_trace(sList[i], start_char, delim);
        trace_list.push_back(vec);
    }
    fin.close();
}

std::vector<std::string> trace_process(std::string strs)
{
    std::vector<std::string> res = {};
    if (strs.size() == 0)
        return res;
    //split trace
    strs += ",";
    size_t pos;
    for (int i = 0; i < strs.size(); i++)
    {
        pos = strs.find(",", i);
        if (pos < strs.size())
        {
            std::string s = strs.substr(i, pos - i);
            res.push_back(s);
            i = pos;
        }
    }
    return res;
}