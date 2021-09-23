#ifndef TRACE_PROCESS_H
#define TRACE_PROCESS_H
#include <vector>
#include <string>

std::vector<std::string> split_trace(const std::string& str, const std::string& start_char, const std::string& delim);
void makeTraceList(std::string fileName, std::vector<std::vector<std::string>>& trace_list);
std::vector<std::string> trace_process(std::string fileName);
#endif
