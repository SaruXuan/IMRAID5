#ifndef IMR_H
#define IMR_H
#include <iostream>
#include <vector>
#include <fstream>
#include <string>
#include <map>
#include <stdint.h>

extern size_t RMW_NUM;
extern size_t UPDATE_NUM;

typedef struct LATENCY_REPORT
{
    size_t seek_count;
    double rotate_count;
} LATENCY_REPORT;

typedef struct ENTRY_4K
{
    unsigned __int64 block_num;
    int offset;
} ENTRY_4K;

typedef struct LATENCY_AND_P_ADDRESS_REPORT
{
    double total_latency;
    std::map<size_t, size_t> mapping_entries; // LtoP
    std::map<size_t, ENTRY_4K> mapping_entries_4k;
} TOTAL_REPORT;

class IMR_Baseline
{
public:
    IMR_Baseline(const unsigned __int64 size, const size_t block_size, const size_t block_per_track);
    void show_mapping();
    void show_mapping_4k();
    bool isInMap(unsigned __int64 address);
    bool isInMap_4k(unsigned __int64 address);
    bool isNeighborTopTracksValid_by_P_address(unsigned __int64 p_address);
    bool isNeighborTopTracksValid_by_track(unsigned __int64 track);
    void buffer_to_continuous_vec(std::map<unsigned __int64, int>& involved_tracks, std::vector<std::vector<unsigned __int64>>& involved_continuous_tracks);
    TOTAL_REPORT write_blocks_from_buffer(std::vector<unsigned __int64>& buffer_vec); //return latency
    TOTAL_REPORT write_4k_from_buffer(std::vector<std::vector<unsigned __int64>>& buffer_vec_4k);
    void write_512_to_4k(std::vector<unsigned __int64>& block_4k, int8_t* tracks, int element);
    void map_512_to_4k(std::vector<unsigned __int64>& block_4k, std::map<unsigned __int64, ENTRY_4K>& LtoP_map_4k, unsigned __int64& write_pointer, TOTAL_REPORT& res_report);
    TOTAL_REPORT read_blocks_from_buffer(std::vector<unsigned __int64>& buffer_vec);
    TOTAL_REPORT read_4k_from_buffer(std::vector<std::vector<unsigned __int64>>& buffer_vec);
    void write_block(unsigned __int64 LBA, int data);

    unsigned __int64 totalBytes = 0;
    size_t sectorSize = 512;
    size_t bottomSectorsPerTracks = 4;
    size_t topSectorsPerTracks = 3;
    unsigned __int64 bottomTrackNumber, topTrackNumber;
    unsigned __int64 totalSectorNumber, bottomSectorNumber, topSectorNumber;
    unsigned __int64 write_pointer = 0;
    unsigned __int64 write_count, read_count, seek_count, update_num, RMW_num;
    short seek_time, rotate_time; //in milisec
    double rotate_count;
    std::vector<bool> bottom_track_status, top_track_status;
    std::map<unsigned __int64, unsigned __int64> LtoP_map; //when L is negative meaning it's parity data
    std::map<unsigned __int64, ENTRY_4K> LtoP_map_4k;
};
#endif
