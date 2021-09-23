#include "assert.h"
#include "raid.h"
#include "trace_process.h"
#include <dirent.h>
#include <iostream>
#include <fstream>
#include <set>
#include <thread>

#define BLOCK_SIZE 4096
#define BLOCKS_PER_TRACK 512
#define DISK_NUM 3

// global variable
unsigned __int64 IMR_SIZE;

void process_trace(std::string trace_name);

int main()
{
    //set imr size
    double gb = 0.0;
    std::cout << "Enter IMR Size in GB: ";
    std::cin >> gb;
    IMR_SIZE = gb * 1000000000;
    while (IMR_SIZE % 7 != 0 || IMR_SIZE % BLOCK_SIZE != 0)
    {
        IMR_SIZE--;
    }
    // trace list
    //std::vector<std::string> trace_list = {"hm0.txt", "prn0.txt", "prxy0.txt", "proj0.txt", "web0.txt"};
    //declare thread
    //std::thread first_thread(process_trace, trace_list[0]);
    //std::thread second_thread(process_trace, trace_list[1]);
    //std::thread third_thread(process_trace, trace_list[2]);
    //std::thread forth_thread(process_trace, trace_list[3]);
    //std::thread fifth_thread(process_trace, trace_list[4]);

    //first_thread.join();
    //second_thread.join();
    //third_thread.join();
    //forth_thread.join();
    //fifth_thread.join();

    RAID5Controller raid5(DISK_NUM, IMR_SIZE, BLOCK_SIZE, BLOCKS_PER_TRACK);
    raid5.print_info();

    std::string trace_path = "D:\\trace\\sequential\\";
    std::string trace_file = "0GB.txt";
    std::ifstream fin(trace_path + trace_file);
    std::cout << "[" << trace_file << "]\n";
    std::string strs;
    if (!fin)
    {
        std::cerr << "Error happens when read file\n";
    }

    unsigned __int64 num_of_trace = 0;
    while (std::getline(fin, strs))
        num_of_trace++;
    fin.clear();
    fin.seekg(0, std::ios::beg);

    unsigned __int64 trace_counter = 1;
    unsigned __int64 trace_write_amount = 0;

    while (std::getline(fin, strs))
    {
        std::vector<std::string> input_request = trace_process(strs);
        if (input_request[3] == "Write")
        {
            raid5.write_request_to_buffer_4k(input_request);
            raid5.flush_buffer_4k(operation::WRITE);
            trace_write_amount += stoull(input_request[5]);
        }
        else if (input_request[3] == "Read")
        {
            raid5.read_request_to_buffer_4k(input_request);
            raid5.flush_buffer_4k(operation::READ);
        }
        printf("\rProgress: %llu / %llu", trace_counter, num_of_trace);
        ++trace_counter;
    }
    printf("\n");
    fin.close();

    //some args set to 0
    raid5.total_latency = 0.0;
    raid5.total_write_latency = 0.0;
    raid5.total_read_latency = 0.0;
    raid5.write_max_latency = 0.0;
    raid5.write_min_latency = 9999.99;
    raid5.read_max_latency = 0.0;
    raid5.read_min_latency = 9999.99;
    raid5.write_count = 0;
    raid5.read_count = 0;

    // full the system with real trace
    trace_file = "proj0.txt";
    trace_path = "D:\\trace\\";
    fin.open(trace_path + trace_file);
    std::cout << "[" << trace_file << "]\n";
    if (!fin)
    {
        std::cerr << "Error happens when read file\n";
    }
    num_of_trace = 0;
    while (std::getline(fin, strs))
        num_of_trace++;
    fin.clear();
    fin.seekg(0, std::ios::beg);
    trace_counter = 1;
    trace_write_amount = 0;
    size_t num_of_RMW_break = 20;
    size_t write_trace_counter = 0;
    size_t read_trace_counter = 0;

    //fout
    trace_file = trace_file.substr(0, trace_file.find("."));
    std::string trace_res_path = "D:\\trace\\usage\\";
    std::ofstream fout(trace_res_path + trace_file + "_0GB_" + std::to_string(DISK_NUM) + "_" + std::to_string(IMR_SIZE / 1000000000) + "_iraid5_result.txt", std::ofstream::trunc);
    fout.setf(std::ios::fixed);

    while (std::getline(fin, strs))
    {
        if (trace_counter % (num_of_trace / num_of_RMW_break) == 0)
        {
            unsigned __int64 tmp = 0;
            for (int i = 0; i < DISK_NUM; i++)
            {
                tmp += raid5.imr_vec[i].RMW_num;
            }
            std::cout << "RMW number: " << tmp << "\n";
            fout << "RMW number: " << tmp << "\n";
        }
        std::vector<std::string> input_request = trace_process(strs);
        if (input_request[3] == "Write")
        {
            write_trace_counter++;
            raid5.write_request_to_buffer_4k(input_request);
            raid5.flush_buffer_4k(operation::WRITE);
            trace_write_amount += stoull(input_request[5]);
        }
        else if (input_request[3] == "Read")
        {
            read_trace_counter++;
            raid5.read_request_to_buffer_4k(input_request);
            raid5.flush_buffer_4k(operation::READ);
        }
        printf("\rProgress: %llu / %llu", trace_counter, num_of_trace);
        ++trace_counter;
    }
    printf("\n");

    //count how many 4k blocks are used
    std::set<unsigned __int64> st;
    unsigned __int64 used_blocks = 0;
    for (size_t j = 0; j < DISK_NUM; j++)
    {
        for (auto& i : raid5.global_LtoP_map_4k)
        {
            if (i.second.disk_num == j)
            {
                st.insert(i.second.p_address.block_num);
            }
        }
        used_blocks += st.size();
        st.clear();
    }

    // std::cout << "After: \n";
    // for (int i = 0; i < DISK_NUM; i++)
    // {
    //     std::cout << "========[IMR" << i << "]========\n";
    //     raid5.imr_vec[i].print_2D_4k();
    //     raid5.imr_vec[i].show_mapping_4k();
    //     std::cout << "\n";
    // }

    // raid5.print_map_4k();

    unsigned __int64 total_RMW_num = 0;
    unsigned __int64 total_update_num = 0;
    unsigned __int64 total_seek_count = 0;
    double total_rotate_count = 0.0;
    unsigned __int64 total_write_amount = 0;
    double WA = 0.0;
    for (int i = 0; i < DISK_NUM; i++)
    {
        total_RMW_num += raid5.imr_vec[i].RMW_num;
        total_update_num += raid5.imr_vec[i].update_num;
        total_seek_count += raid5.imr_vec[i].seek_count;
        total_rotate_count += raid5.imr_vec[i].rotate_count;
    }
    total_write_amount = BLOCK_SIZE * (total_RMW_num * BLOCKS_PER_TRACK * (5 / 2) + raid5.total_new_write_blocks + raid5.total_update_blocks);
    WA = (double)total_write_amount / (double)trace_write_amount;
    double write_avg_latency = 0.0;
    double read_avg_latency = 0.0;

    if (raid5.total_write_latency / raid5.write_count > raid5.write_max_latency || raid5.total_write_latency / raid5.write_count < raid5.write_min_latency)
    {
        write_avg_latency = (raid5.write_max_latency + raid5.write_min_latency) / 3;
    }
    else
    {
        write_avg_latency = raid5.total_write_latency / write_trace_counter;
    }

    if (raid5.total_read_latency / raid5.read_count > raid5.read_max_latency || raid5.total_read_latency / raid5.read_count < raid5.read_min_latency)
    {
        read_avg_latency = (raid5.read_max_latency + raid5.read_min_latency) / 3;
    }
    else
    {
        read_avg_latency = raid5.total_read_latency / read_trace_counter;
    }

    std::cout.setf(std::ios::fixed);
    std::cout << "========[Result]========\n";
    std::cout << "TRACE WRITE AMOUNT: " << trace_write_amount << "\n";
    std::cout << "RMW NUMBER: " << total_RMW_num << "\n";
    std::cout << "UPDATE NUMBER: " << total_update_num << "\n";
    std::cout << "WRITE REQUEST COUNT: " << raid5.write_count << "\n";
    std::cout << "READ REQUEST COUNT: " << raid5.read_count << "\n";
    std::cout << "TOTAL SEEK COUNT: " << total_seek_count << "\n";
    std::cout << "TOTAL ROTATE COUNT: " << total_rotate_count << "\n";
    std::cout << "TOTAL WRITE LATENCY WITH PARALLEL: " << raid5.total_write_latency << "\n";
    std::cout << "TOTAL READ LATENCY WITH PARALLEL: " << raid5.total_read_latency << "\n";
    std::cout << "TOTAL LATENCY WITH PARALLEL: " << raid5.total_latency << "\n";
    std::cout << "TOTAL NEW WRITE BLOCKS: " << raid5.total_new_write_blocks << "\n";
    std::cout << "TOTAL WA: " << WA << "\n";
    std::cout << "USED 4K BLOCKS: " << used_blocks << "\n";
    std::cout << "MAX WRITE LATENCY: " << raid5.write_max_latency << "\n";
    std::cout << "MIN WRITE LATENCY: " << raid5.write_min_latency << "\n";
    std::cout << "AVG WRITE LATENCY: " << write_avg_latency << "\n";
    std::cout << "MAX READ LATENCY: " << raid5.read_max_latency << "\n";
    std::cout << "MIN READ LATENCY: " << raid5.read_min_latency << "\n";
    std::cout << "AVG READ LATENCY: " << read_avg_latency << "\n";
    std::cout << "REAL AVG WRITE LATENCY: " << raid5.total_write_latency / raid5.write_count << "\n";
    std::cout << "REAL AVG READ LATENCY: " << raid5.total_read_latency / raid5.read_count << "\n";

    fout << "========[Result]========\n";
    fout << "RMW NUMBER: " << total_RMW_num << "\n";
    fout << "UPDATE NUMBER: " << total_update_num << "\n";
    fout << "WRITE REQUEST COUNT: " << raid5.write_count << "\n";
    fout << "READ REQUEST COUNT: " << raid5.read_count << "\n";
    fout << "TOTAL SEEK COUNT: " << total_seek_count << "\n";
    fout << "TOTAL ROTATE COUNT: " << total_rotate_count << "\n";
    fout << "TOTAL WRITE LATENCY WITH PARALLEL: " << raid5.total_write_latency << "\n";
    fout << "TOTAL READ LATENCY WITH PARALLEL: " << raid5.total_read_latency << "\n";
    fout << "TOTAL LATENCY WITH PARALLEL: " << raid5.total_latency << "\n";
    fout << "TOTAL NEW WRITE BLOCKS: " << raid5.total_new_write_blocks << "\n";
    fout << "TOTAL WA: " << WA << "\n";
    fout << "USED 4K BLOCKS: " << used_blocks << "\n";
    fout << "MAX WRITE LATENCY: " << raid5.write_max_latency << "\n";
    fout << "MIN WRITE LATENCY: " << raid5.write_min_latency << "\n";
    fout << "AVG WRITE LATENCY: " << write_avg_latency << "\n";
    fout << "MAX READ LATENCY: " << raid5.read_max_latency << "\n";
    fout << "MIN READ LATENCY: " << raid5.read_min_latency << "\n";
    fout << "AVG READ LATENCY: " << read_avg_latency << "\n";

    fout.close();
    fin.close();

    std::cout << trace_file << " finished." << "\n";

    return 0;
}

void process_trace(std::string trace_name)
{
    RAID5Controller raid5(DISK_NUM, IMR_SIZE, BLOCK_SIZE, BLOCKS_PER_TRACK);
    raid5.print_info();

    std::string trace_path = "D:\\trace\\sequential\\";
    std::string trace_file = "30GB.txt";
    std::ifstream fin(trace_path + trace_file);
    std::cout << "[" << trace_file << "]\n";
    std::string strs;
    if (!fin)
    {
        std::cerr << "Error happens when read file\n";
    }

    unsigned __int64 num_of_trace = 0;
    while (std::getline(fin, strs))
        num_of_trace++;
    fin.clear();
    fin.seekg(0, std::ios::beg);

    unsigned __int64 trace_counter = 1;
    unsigned __int64 trace_write_amount = 0;

    while (std::getline(fin, strs))
    {
        std::vector<std::string> input_request = trace_process(strs);
        if (input_request[3] == "Write")
        {
            raid5.write_request_to_buffer_4k(input_request);
            raid5.flush_buffer_4k(operation::WRITE);
            trace_write_amount += stoull(input_request[5]);
        }
        else if (input_request[3] == "Read")
        {
            raid5.read_request_to_buffer_4k(input_request);
            raid5.flush_buffer_4k(operation::READ);
        }
        printf("\rProgress: %llu / %llu", trace_counter, num_of_trace);
        ++trace_counter;
    }
    printf("\n");
    fin.close();

    // full the system with real trace
    trace_path = "D:\\trace\\";
    fin.open(trace_path + trace_name);
    std::cout << "[" << trace_name << "]\n";
    if (!fin)
    {
        std::cerr << "Error happens when read file\n";
    }
    num_of_trace = 0;
    while (std::getline(fin, strs))
        num_of_trace++;
    fin.clear();
    fin.seekg(0, std::ios::beg);
    trace_counter = 1;
    trace_write_amount = 0;
    size_t num_of_RMW_break = 20;
    size_t write_trace_counter = 0;
    size_t read_trace_counter = 0;

    //fout
    trace_file = trace_file.substr(0, trace_file.find("."));
    std::string trace_res_path = "D:\\trace_result\\";
    std::ofstream fout(trace_res_path + trace_name + "_result.txt", std::ofstream::trunc);
    fout.setf(std::ios::fixed);


    while (std::getline(fin, strs))
    {
        std::vector<std::string> input_request = trace_process(strs);
        if (input_request[3] == "Write")
        {
            write_trace_counter++;
            raid5.write_request_to_buffer_4k(input_request);
            raid5.flush_buffer_4k(operation::WRITE);
            trace_write_amount += stoull(input_request[5]);
        }
        else if (input_request[3] == "Read")
        {
            read_trace_counter++;
            raid5.read_request_to_buffer_4k(input_request);
            raid5.flush_buffer_4k(operation::READ);
        }
        printf("\rProgress: %llu / %llu", trace_counter, num_of_trace);
        ++trace_counter;
    }
    printf("\n");

    //count how many 4k blocks are used
    std::set<unsigned __int64> st;
    unsigned __int64 used_blocks = 0;
    for (size_t j = 0; j < DISK_NUM; j++)
    {
        for (auto& i : raid5.global_LtoP_map_4k)
        {
            if (i.second.disk_num == j)
            {
                st.insert(i.second.p_address.block_num);
            }
        }
        used_blocks += st.size();
        st.clear();
    }

    // std::cout << "After: \n";
    // for (int i = 0; i < DISK_NUM; i++)
    // {
    //     std::cout << "========[IMR" << i << "]========\n";
    //     raid5.imr_vec[i].print_2D_4k();
    //     raid5.imr_vec[i].show_mapping_4k();
    //     std::cout << "\n";
    // }

    // raid5.print_map_4k();

    unsigned __int64 total_RMW_num = 0;
    unsigned __int64 total_update_num = 0;
    unsigned __int64 total_seek_count = 0;
    double total_rotate_count = 0.0;
    unsigned __int64 total_write_amount = 0;
    double WA = 0.0;
    for (int i = 0; i < DISK_NUM; i++)
    {
        total_RMW_num += raid5.imr_vec[i].RMW_num;
        total_update_num += raid5.imr_vec[i].update_num;
        total_seek_count += raid5.imr_vec[i].seek_count;
        total_rotate_count += raid5.imr_vec[i].rotate_count;
    }
    total_write_amount = BLOCK_SIZE * (total_RMW_num * BLOCKS_PER_TRACK * (5 / 2) + raid5.total_new_write_blocks + raid5.total_update_blocks);
    WA = (double)total_write_amount / (double)trace_write_amount;
    double write_avg_latency = 0.0;
    double read_avg_latency = 0.0;

    if (raid5.total_write_latency / write_trace_counter > raid5.write_max_latency || raid5.total_write_latency / write_trace_counter < raid5.write_min_latency)
    {
        write_avg_latency = (raid5.write_max_latency + raid5.write_min_latency) / 3;
    }
    else
    {
        write_avg_latency = raid5.total_write_latency / write_trace_counter;
    }

    if (raid5.total_read_latency / read_trace_counter > raid5.read_max_latency || raid5.total_read_latency / read_trace_counter < raid5.read_min_latency)
    {
        read_avg_latency = (raid5.read_max_latency + raid5.read_min_latency) / 2;
    }
    else
    {
        read_avg_latency = raid5.total_read_latency / read_trace_counter;
    }

    std::cout.setf(std::ios::fixed);
    std::cout << "========[Result]========\n";
    std::cout << "TRACE WRITE AMOUNT: " << trace_write_amount << "\n";
    std::cout << "RMW NUMBER: " << total_RMW_num << "\n";
    std::cout << "UPDATE NUMBER: " << total_update_num << "\n";
    std::cout << "WRITE REQUEST COUNT: " << raid5.write_count << "\n";
    std::cout << "READ REQUEST COUNT: " << raid5.read_count << "\n";
    std::cout << "TOTAL SEEK COUNT: " << total_seek_count << "\n";
    std::cout << "TOTAL ROTATE COUNT: " << total_rotate_count << "\n";
    std::cout << "TOTAL WRITE LATENCY WITH PARALLEL: " << raid5.total_write_latency << "\n";
    std::cout << "TOTAL READ LATENCY WITH PARALLEL: " << raid5.total_read_latency << "\n";
    std::cout << "TOTAL LATENCY WITH PARALLEL: " << raid5.total_latency << "\n";
    std::cout << "TOTAL NEW WRITE BLOCKS: " << raid5.total_new_write_blocks << "\n";
    std::cout << "TOTAL WA: " << WA << "\n";
    std::cout << "USED 4K BLOCKS: " << used_blocks << "\n";
    std::cout << "MAX WRITE LATENCY: " << raid5.write_max_latency << "\n";
    std::cout << "MIN WRITE LATENCY: " << raid5.write_min_latency << "\n";
    std::cout << "AVG WRITE LATENCY: " << write_avg_latency << "\n";
    std::cout << "MAX READ LATENCY: " << raid5.read_max_latency << "\n";
    std::cout << "MIN READ LATENCY: " << raid5.read_min_latency << "\n";
    std::cout << "AVG READ LATENCY: " << read_avg_latency << "\n";
    std::cout << "REAL AVG WRITE LATENCY: " << raid5.total_write_latency / write_trace_counter << "\n";
    std::cout << "REAL AVG READ LATENCY: " << raid5.total_read_latency / read_trace_counter << "\n";

    fout << "========[Result]========\n";
    fout << "RMW NUMBER: " << total_RMW_num << "\n";
    fout << "UPDATE NUMBER: " << total_update_num << "\n";
    fout << "WRITE REQUEST COUNT: " << raid5.write_count << "\n";
    fout << "READ REQUEST COUNT: " << raid5.read_count << "\n";
    fout << "TOTAL SEEK COUNT: " << total_seek_count << "\n";
    fout << "TOTAL ROTATE COUNT: " << total_rotate_count << "\n";
    fout << "TOTAL WRITE LATENCY WITH PARALLEL: " << raid5.total_write_latency << "\n";
    fout << "TOTAL READ LATENCY WITH PARALLEL: " << raid5.total_read_latency << "\n";
    fout << "TOTAL LATENCY WITH PARALLEL: " << raid5.total_latency << "\n";
    fout << "TOTAL NEW WRITE BLOCKS: " << raid5.total_new_write_blocks << "\n";
    fout << "TOTAL WA: " << WA << "\n";
    fout << "USED 4K BLOCKS: " << used_blocks << "\n";
    fout << "MAX WRITE LATENCY: " << raid5.write_max_latency << "\n";
    fout << "MIN WRITE LATENCY: " << raid5.write_min_latency << "\n";
    fout << "AVG WRITE LATENCY: " << write_avg_latency << "\n";
    fout << "MAX READ LATENCY: " << raid5.read_max_latency << "\n";
    fout << "MIN READ LATENCY: " << raid5.read_min_latency << "\n";
    fout << "AVG READ LATENCY: " << read_avg_latency << "\n";

    fout.close();
    fin.close();

    std::cout << trace_name << " finished." << "\n";
}

// 執行程式: Ctrl + F5 或 [偵錯] > [啟動但不偵錯] 功能表
// 偵錯程式: F5 或 [偵錯] > [啟動偵錯] 功能表

// 開始使用的提示: 
//   1. 使用 [方案總管] 視窗，新增/管理檔案
//   2. 使用 [Team Explorer] 視窗，連線到原始檔控制
//   3. 使用 [輸出] 視窗，參閱組建輸出與其他訊息
//   4. 使用 [錯誤清單] 視窗，檢視錯誤
//   5. 前往 [專案] > [新增項目]，建立新的程式碼檔案，或是前往 [專案] > [新增現有項目]，將現有程式碼檔案新增至專案
//   6. 之後要再次開啟此專案時，請前往 [檔案] > [開啟] > [專案]，然後選取 .sln 檔案
