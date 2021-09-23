#include "raid.h"
#include <iostream>

RAID5Controller::RAID5Controller(size_t num_of_disks, unsigned __int64 each_imr_size, size_t block_size, size_t blocks_per_track)
{
    disk_num = num_of_disks;
    total_size = num_of_disks * each_imr_size;
    write_pointer = 0;
    update_num = 0;
    RMW_num = 0;
    write_count = 0;
    read_count = 0;
    total_latency = 0.0;
    total_write_latency = 0.0;
    total_read_latency = 0.0;
    write_max_latency = 0.0;
    read_max_latency = 0.0;
    write_avg_latency = 0.0;
    read_avg_latency = 0.0;
    write_min_latency = 9999.99;
    read_min_latency = 9999.99;
    stripe_counter = -1; //goes negative, start with -1
    parity_gen_counter = 0;
    total_new_write_blocks = 0;
    total_update_blocks = 0;
    block_count = 0;
    counts_for_change_stripe_group = 0;
    this->block_size = block_size;
    sector_size = 512;
    write_order = Get_Write_Order(disk_num);
    for (size_t i = 0; i < num_of_disks; i++)
    {
        IMR_Baseline imr(each_imr_size, block_size, blocks_per_track, num_of_disks);
        imr_vec.push_back(imr);
    }
    buffer_vec.resize(disk_num);
    buffer_vec_4k.resize(disk_num);
}

RAID5Controller::~RAID5Controller()
{
    /*for (size_t i = 0; i < disk_num; i++)
    {
        imr_vec[i].~IMR_Baseline();
    }*/
}

void RAID5Controller::print_write_order(int times)
{
    ORDER* temp = write_order;
    for (size_t i = 0; i < times * disk_num * disk_num; i++)
    {
        std::cout << temp->val << " ";
        temp = temp->next;
    }
    std::cout << "\n";
}

void RAID5Controller::write_request_to_buffer(std::vector<std::string>& request)
{
    unsigned __int64 max_size_t = (unsigned __int64)-1; // size_t max indicates parity data
    if (request[3] == "Write")
    {
        //write count
        write_count++;
        unsigned __int64 offset = stoull(request[4]) / sector_size;
        unsigned __int64 n_sectors = stoull(request[5]) / sector_size; //sector size
        //distribute blocks to corresponding buffer
        for (unsigned __int64 j = 0; j < n_sectors; j++)
        {
            //first check for updates in global LtoP map
            if (global_LtoP_map.count(offset + j) > 0)
            {
                buffer_vec[global_LtoP_map[offset + j].disk_num].push_back(offset + j);
            }
            else //or distribute new write
            {
                buffer_vec[write_order->val].push_back(offset + j);
                write_order = write_order->next;
                if (write_order->is_parity)
                {
                    buffer_vec[write_order->val].push_back(max_size_t); // size_t max indicates parity data
                    write_order = write_order->next;
                }
                total_new_write_blocks++;
            }
        }
    }
}

void RAID5Controller::write_request_to_buffer_4k(std::vector<std::string> request)
{
    unsigned __int64 max_size_t = (unsigned __int64)-1; // size_t max indicates parity data
    if (request[3] == "Write")
    {
        //write count
        write_count++;
        unsigned __int64 offset = stoull(request[4]) / sector_size;
        unsigned __int64 n_sectors = stoull(request[5]) / sector_size; //sector size
        std::vector<unsigned __int64> parity_4k(8, max_size_t);
        std::vector<unsigned __int64> empty_vec = {};
        bool finish_without_full_block = true;
        bool switch_to_next_block = true;
        //distribute blocks to corresponding buffer
        for (unsigned __int64 j = 0; j < n_sectors; j++)
        {
            //first check for updates in global LtoP map
            if (global_LtoP_map_4k.count(offset + j) > 0)
            {
                buffer_vec_4k[global_LtoP_map_4k[offset + j].disk_num].push_back({ offset + j });
                total_update_blocks++;
                //update will own its vector
            }
            else //or distribute new write
            {
                //4k
                finish_without_full_block = true;
                if (switch_to_next_block)
                {
                    buffer_vec_4k[write_order->val].push_back({offset + j});
                    switch_to_next_block = false;
                }
                else
                {
                    buffer_vec_4k[write_order->val].back().push_back(offset + j); // write lba to buffer_vec_4k
                }

                //below are the preparation for the next write
                if (buffer_vec_4k[write_order->val].back().size() == 8) // when a 4k block is fully filled, it needs to switch to the next 4k block
                {
                    switch_to_next_block = true;
                    finish_without_full_block = false;
                    total_new_write_blocks++;
                    if (imr_vec[0].bottomSectorsPerTracks == block_count + 1) // when counts_for_change_disk == 4, meaning a track is fully filled
                    {
                        write_order = write_order->next;
                        if (write_order->is_parity) // if the next write order is parity
                        {
                            if (counts_for_change_stripe_group == disk_num - 1) // when counts_for_change_stripe_group == 3, meaning the virtual frame group is ready
                            {
                                // generate parity and assign to buffer
                                for (size_t i = 0; i < disk_num; i++)
                                {
                                    buffer_vec_4k[i].push_back(parity_4k);
                                }
                                write_order = write_order->next;
                                block_count = 0;
                                counts_for_change_stripe_group = 0;
                            }
                            else // if virtual frame group not yet ready, just forward the write order
                            {
                                write_order = write_order->next;
                                block_count = 0;
                                counts_for_change_stripe_group++;
                            }
                        }
                        else
                        {
                            block_count = 0;
                        }
                    }
                    else
                    {
                        block_count++;
                    }
                }
            }
        }
        if (finish_without_full_block)
        {
            total_new_write_blocks++;
            if (imr_vec[0].bottomSectorsPerTracks == block_count + 1) // when counts_for_change_disk == 4, meaning a track is fully filled
            {
                write_order = write_order->next;
                if (write_order->is_parity) // if the next write order is parity
                {
                    if (counts_for_change_stripe_group == disk_num) // when counts_for_change_stripe_group == 3, meaning the virtual frame group is ready
                    {
                        // generate parity and assign to buffer
                        for (size_t i = 0; i < disk_num; i++)
                        {
                            buffer_vec_4k[i].push_back(parity_4k);
                        }
                    }
                    else // if virtual frame group not yet ready, just forward the write order
                    {
                        write_order = write_order->next;
                        counts_for_change_stripe_group++;
                    }
                }
                else
                {
                    block_count = 0;
                }
            }
            else
            {
                block_count++;
            }
        }
    }
}

void RAID5Controller::read_request_to_buffer(std::vector<std::string> request)
{
    if (request[3] == "Read")
    {
        //read count
        unsigned __int64 offset = stoull(request[4]) >> 9;
        unsigned __int64 n_sectors = stoull(request[5]) / sector_size; //sector size
        for (unsigned __int64 j = 0; j < n_sectors; j++)
        {
            //first check global LtoP map
            if (global_LtoP_map.count(offset + j) > 0)
            {
                buffer_vec[global_LtoP_map[offset + j].disk_num].push_back(offset + j);
                read_count++;
            }
            // else //not found
            // {
            //     std::cout << "Read LBA " << offset + j << " Not Found\n";
            // }
        }
    }
}

void RAID5Controller::read_request_to_buffer_4k(std::vector<std::string> request)
{
    if (request[3] == "Read")
    {
        //read count
        read_count++;
        unsigned __int64 offset = stoull(request[4]) / sector_size;
        unsigned __int64 n_sectors = stoull(request[5]) / sector_size; //sector size
        for (unsigned __int64 j = 0; j < n_sectors; j++)
        {
            //first check global LtoP map
            if (global_LtoP_map_4k.count(offset + j) > 0)
            {
                buffer_vec_4k[global_LtoP_map_4k[offset + j].disk_num].push_back({ offset + j });
            }
            // else //not found
            // {
            //     std::cout << "Read LBA " << offset + j << " Not Found\n";
            // }
        }
    }
}

void RAID5Controller::flush_buffer_4k(operation op)
{
    double res_latency = 0.0;
    for (size_t i = 0; i < disk_num; i++)
    {
        //flush
        if (buffer_vec_4k[i].size() > 0)
        {
            if (op == operation::WRITE)
            {
                TOTAL_REPORT tmp_report;
                tmp_report = imr_vec[i].write_4k_from_buffer(buffer_vec_4k[i]);
                res_latency = (tmp_report.total_latency > res_latency) ? tmp_report.total_latency : res_latency;
                //report mapping to raid
                for (auto j = tmp_report.mapping_entries_4k.begin(); j != tmp_report.mapping_entries_4k.end(); j++)
                {
                    global_LtoP_map_4k[j->first].disk_num = i;
                    global_LtoP_map_4k[j->first].p_address.block_num = j->second.block_num;
                    global_LtoP_map_4k[j->first].p_address.offset = j->second.offset;
                    global_LtoP_map_4k[j->first].p_address.valid = j->second.valid;
                }
                total_write_latency += res_latency;
            }
            else if (op == operation::READ)
            {
                TOTAL_REPORT tmp_report;
                tmp_report = imr_vec[i].read_4k_from_buffer(buffer_vec_4k[i]);
                res_latency = (tmp_report.total_latency > res_latency) ? tmp_report.total_latency : res_latency;
                total_read_latency += res_latency;
            }
        }
    }
    //clean buffer
    for (size_t i = 0; i < disk_num; i++)
    {
        buffer_vec_4k[i].clear();
    }
    //report max counts to raid
    total_latency += res_latency;
    if (op == operation::WRITE)
    {
        write_max_latency = (res_latency > write_max_latency && res_latency != 0.0) ? res_latency : write_max_latency;
        write_min_latency = (res_latency < write_min_latency && res_latency != 0.0) ? res_latency : write_min_latency;
    }   
    else
    {
        read_max_latency = (res_latency > read_max_latency && res_latency != 0.0) ? res_latency : read_max_latency;
        read_min_latency = (res_latency < read_min_latency && res_latency != 0.0) ? res_latency : read_min_latency;
    }
}

bool RAID5Controller::isInMap(unsigned __int64 LBA)
{
    if (global_LtoP_map.count(LBA) > 0)
        return true;
    else
        return false;
}

void RAID5Controller::print_map()
{
    std::cout << "========[Global Map]========"
        << "\n";
    for (auto it = global_LtoP_map.begin(); it != global_LtoP_map.end(); it++)
    {
        std::cout << "[" << it->first << "]: (" << it->second.disk_num << ") " << it->second.p_address << "\n";
    }
}

void RAID5Controller::print_map_4k()
{
    std::cout << "========[Global Map]========"
        << "\n";
    for (auto it = global_LtoP_map_4k.begin(); it != global_LtoP_map_4k.end(); it++)
    {
        std::cout << "[" << it->first << "]: (" << it->second.disk_num << ") PBA " << it->second.p_address.block_num << " offset " << it->second.p_address.offset << "\n";
    }
}

void RAID5Controller::print_info()
{
    std::cout << "========[IMR RAID 5 Info]========"
        << "\n";
    std::cout << "Number of Disk: " << disk_num << "\n";
    std::cout << "Total Data Size: " << total_size << "\n";
    for (size_t i = 0; i < disk_num; i++)
    {
        std::cout << "IMR" << i << " Size: " << imr_vec[i].totalBytes << "\n";
    }
    std::cout << "Block Size: " << block_size << " bytes\n";
    std::cout << "Bottom Blocks Per Tracks: " << imr_vec[0].bottomSectorsPerTracks << "\n";
    std::cout << "Top Blocks Per Tracks: " << imr_vec[0].topSectorsPerTracks << "\n";
}

//Below are the functions for generating RAID 5 Write order linked list
ORDER* Get_Write_Order(size_t disk_num)
{
    ORDER* head = NULL;
    int RR_counter = 0;
    int parity_ptr = disk_num - 1;

    while (parity_ptr >= 0)
    {
        for (size_t i = 0; i < disk_num; i++)
        {
            if (parity_ptr != RR_counter)
            {
                Append_Node(head, RR_counter, false);
                RR_counter = (RR_counter + 1) % disk_num;
            }
            else
            {
                RR_counter = (RR_counter + 1) % disk_num;
            }
        }
        Append_Node(head, parity_ptr, true);
        parity_ptr--;
    }
    Make_Cycle(head);
    return head;
}

void Append_Node(ORDER*& current, int val, bool is_parity)
{
    ORDER* newNode = new ORDER();
    newNode->val = val;
    newNode->is_parity = is_parity;
    if (current == NULL)
    {
        current = newNode;
        return;
    }
    ORDER* temp = current;
    while (temp->next != NULL)
    {
        temp = temp->next;
    }
    temp->next = newNode;
}

ORDER* Get_Tail(ORDER*& current)
{
    if (current == NULL)
        return NULL;
    ORDER* temp = current;
    while (temp->next != NULL)
    {
        temp = temp->next;
    }
    return temp;
}

void Make_Cycle(ORDER*& current)
{
    ORDER* tail = Get_Tail(current);
    tail->next = current;
}