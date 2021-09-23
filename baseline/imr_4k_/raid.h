#ifndef RAID_H
#define RAID_H
#include "imr.h"
#include <vector>
#include <string>
#include <map>

class ORDER
{
public:
    ORDER() : val(0), is_parity(false), next(NULL) {};
    int val;
    bool is_parity;
    ORDER* next;
};

typedef struct LTOP_ENTRY
{
    short disk_num;
    unsigned __int64 p_address;
} ENTRY;

typedef struct LTOP_ENTRY_4K
{
    short disk_num;
    ENTRY_4K p_address;
} LTOP_ENTRY_4K;

enum class operation
{
    WRITE,
    READ
};

class RAID5Controller
{
public:
    RAID5Controller(size_t num_of_disks, unsigned __int64 each_imr_size, size_t block_size, size_t blocks_per_track);
    void write_request(std::vector<std::vector<std::string>> trace_list);
    void write_request_to_buffer(std::vector<std::string>& request);
    void write_request_to_buffer_4k(std::vector<std::string> request);
    void read_request_to_buffer(std::vector<std::string> request);
    void read_request_to_buffer_4k(std::vector<std::string> request);
    void flush_buffer(operation op);
    void flush_buffer_4k(operation op);
    bool isInMap(unsigned __int64 LBA);
    void print_write_order(int times);
    void print_map();
    void print_map_4k();
    void print_info();
    std::vector<IMR_Baseline> imr_vec;
    std::vector<std::vector<unsigned __int64>> buffer_vec;
    std::vector<std::vector<std::vector<unsigned __int64>>> buffer_vec_4k;
    size_t disk_num;
    unsigned __int64 total_size;
    unsigned __int64 write_pointer;
    unsigned __int64 stripe_counter;
    unsigned __int64 parity_gen_counter;
    unsigned __int64 block_size, sector_size;
    unsigned __int64 update_num, RMW_num, write_count, read_count;
    unsigned __int64 total_new_write_blocks;
    unsigned __int64 total_update_blocks;
    double total_write_latency;
    double total_read_latency;
    double total_latency;
    double write_max_latency, write_min_latency, write_avg_latency;
    double read_max_latency, read_min_latency, read_avg_latency;
    ORDER* write_order;
    std::map<unsigned __int64, ENTRY> global_LtoP_map;
    std::map<unsigned __int64, LTOP_ENTRY_4K> global_LtoP_map_4k;
};

ORDER* Get_Write_Order(size_t disk_num);
void Append_Node(ORDER*& current, int val, bool is_parity);
ORDER* Get_Tail(ORDER*& current);
void Make_Cycle(ORDER*& current);
#endif
