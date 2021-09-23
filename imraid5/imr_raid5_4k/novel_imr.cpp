#include "imr.h"
#include <iostream>
#include <vector>
#include <fstream>
#include <string>
#include <map>
#include <set>
#include <cstdlib>

IMR_Baseline::IMR_Baseline(const unsigned __int64 size, const size_t block_size, const size_t block_per_bottom_track, const size_t num_of_disks)
{
    totalBytes = size;
    this->block_size = block_size;
    write_count = 0;
    read_count = 0;
    seek_count = 0;
    rotate_count = 0.0;
    update_num = 0;
    RMW_num = 0;
    seek_time = 8;
    rotate_time = 5;
    write_pointer = 0;
    parity_pointer = 0;
    bottomSectorsPerTracks = block_per_bottom_track;
    raid_num_of_disks = num_of_disks;
    topSectorsPerTracks = bottomSectorsPerTracks * 3 / 4;

    totalSectorNumber = size / block_size;

    bottomSectorNumber = totalSectorNumber * 4 / 7;
    bottomTrackNumber = bottomSectorNumber / bottomSectorsPerTracks;

    topSectorNumber = totalSectorNumber * 3 / 7;
    topTrackNumber = topSectorNumber / topSectorsPerTracks;

    bottom_track_status.resize(bottomTrackNumber, false);
    top_track_status.resize(topTrackNumber, false);

    unsigned __int64 top_cache_nums = topTrackNumber;
    top_cache_mapping_table.resize(top_cache_nums);
    for (unsigned __int64 i = 0; i < top_cache_nums; i++)
    {
        top_cache_mapping_table[i].top_cache_write_pointer = 0;
    }

    top_cache_block_num = topSectorsPerTracks * (num_of_disks - 1) - bottomSectorsPerTracks;
}

IMR_Baseline::~IMR_Baseline()
{
    //delete[] bottom_track_status;
    //delete[] top_track_status;
    /*for (unsigned __int64 i = 0; i < bottomSectorNumber; i++)
    {
        delete[] bottom_tracks_4k[i];
    }
    delete[] bottom_tracks_4k;
    for (unsigned __int64 i = 0; i < topSectorNumber; i++)
    {
        delete[] top_tracks_4k[i];
    }
    delete[] top_tracks_4k;
    delete[] top_cache_mapping_table;*/
}

void IMR_Baseline::show_mapping()
{
    std::cout << "========[Map]========" << std::endl;
    for (auto it = LtoP_map.cbegin(); it != LtoP_map.cend(); it++)
    {
        std::cout << "[" << it->first << "]: " << it->second << "\n";
    }
}

void IMR_Baseline::show_mapping_4k()
{
    std::cout << "========[Map]========" << std::endl;
    for (auto it = LtoP_map_4k.cbegin(); it != LtoP_map_4k.cend(); it++)
    {
        std::cout << "[" << it->first << "]: PBA " << it->second.block_num << " offset " << it->second.offset << " valid " << it->second.valid << "\n";
    }
}

bool IMR_Baseline::isInMap(unsigned __int64 address)
{
    if (LtoP_map.count(address) > 0)
        return true;
    else
        return false;
}

bool IMR_Baseline::isInMap_4k(unsigned __int64 address)
{
    if (LtoP_map_4k.count(address) > 0)
        return true;
    else
        return false;
}

bool IMR_Baseline::isNeighborTopTracksValid_by_P_address(unsigned __int64 p_address)
{
    unsigned __int64 top_track_left_number = p_address / bottomSectorsPerTracks; //bottomtracknumber and top track number are the same
    if (p_address / bottomSectorsPerTracks == bottomTrackNumber - 1)
        if(top_track_left_number < topTrackNumber)
            return top_track_status[top_track_left_number];
    unsigned __int64 top_track_right_number = (p_address / bottomSectorsPerTracks) + 1;
    if(top_track_left_number < topTrackNumber && top_track_right_number <topTrackNumber)
        return top_track_status[top_track_left_number] || top_track_status[top_track_right_number];
    return true;
}

bool IMR_Baseline::isNeighborTopTracksValid_by_track(unsigned __int64 track)
{
    unsigned __int64 first_p_address_of_track = track * bottomSectorsPerTracks;
    return isNeighborTopTracksValid_by_P_address(first_p_address_of_track);
}

void IMR_Baseline::buffer_to_continuous_vec(std::map<unsigned __int64, int>& involved_tracks, std::vector<std::vector<unsigned __int64>>& involved_continuous_tracks)
{
    if (involved_tracks.size() == 0)
        return;
    auto start = involved_tracks.begin();
    auto prev = involved_tracks.begin();
    for (auto i = next(involved_tracks.begin()); i != involved_tracks.end(); i++)
    {
        if (prev->first + 1 == i->first)
        {
            prev++;
        }
        else
        {
            std::vector<unsigned __int64> temp;
            for (; start != i; start++)
            {
                temp.push_back(start->first);
            }
            involved_continuous_tracks.push_back(temp);
            prev++;
        }
    }
    std::vector<unsigned __int64> temp;
    for (; start != involved_tracks.end(); start++)
    {
        temp.push_back(start->first);
    }
    involved_continuous_tracks.push_back(temp);
}

TOTAL_REPORT IMR_Baseline::write_4k_from_buffer(std::vector<std::vector<unsigned __int64>>& buffer_vec_4k)
{
    LATENCY_REPORT latency_report;
    TOTAL_REPORT res_report;
    //LATENCY_REPORT initialization
    latency_report.seek_count = 0;
    latency_report.rotate_count = 0.0;
    res_report.total_latency = 0.0;
    if (buffer_vec_4k.size() == 0)
    {
        std::cout << "Empty buffer vec happens\n";
        return res_report;
    }
    std::map<unsigned __int64, int> involved_bottom_tracks; //involved track number, involved times
    std::map<unsigned __int64, int> involved_top_tracks;
    std::vector<std::vector<unsigned __int64>> new_writes;
    std::vector<unsigned __int64> updates;
    std::set<unsigned __int64> inplace_updates;
    unsigned __int64 parity_track_counts = 0;
    for (auto i = buffer_vec_4k.begin(); i != buffer_vec_4k.end(); i++)
    {
        if ((*i).size() == 0)
            continue;
        if (isInMap_4k((*i)[0])) // update own its vector, so if update, there is only one lba in vector
        {
            update_num++;
            unsigned __int64 p_address = LtoP_map_4k[(*i)[0]].block_num;
            if (LtoP_map_4k[(*i)[0]].valid)
            {
                if (isNeighborTopTracksValid_by_P_address(p_address))
                {
                    updates.push_back((*i)[0]);
                }
            }
            else
            {
                unsigned __int64 top_cache_num = p_address / topSectorsPerTracks * (raid_num_of_disks - 1);
                inplace_updates.insert(top_cache_num);
            }
        }
        else if((*i)[0] == (unsigned __int64)-1)
        {
            parity_track_counts++;
        }
        else
        {
            new_writes.push_back(*i);
        }
    }
    //Below deal with updates
    //calculate the involved tracks
    update_to_top_cache(updates, res_report, latency_report);
    /* for (auto i = updates.begin(); i != updates.end(); i++)
    {
        unsigned __int64 update_track = LtoP_map_4k[*i].block_num / bottomSectorsPerTracks;
        involved_bottom_tracks[update_track] = 1;
    }
    for (auto i = involved_bottom_tracks.begin(); i != involved_bottom_tracks.end(); i++)
    {
        unsigned __int64 bottom_track_num = i->first;
        if (isNeighborTopTracksValid_by_track(bottom_track_num))
        {

        }
    }*/ 

    //inplace updates
    seek_count += inplace_updates.size();
    latency_report.seek_count += inplace_updates.size();
    rotate_count += 0.5 * inplace_updates.size();
    latency_report.rotate_count += 0.5 * inplace_updates.size();
    
    //Below deal with user data new writes
    //count seek rotation
    size_t new_write_count = new_writes.size();
    if (new_write_count > 0)
    {
        size_t rest_blocks = bottomSectorsPerTracks - (write_pointer % bottomSectorsPerTracks);
        if (rest_blocks > new_write_count)
        {
            seek_count++;
            latency_report.seek_count++;
            rotate_count += 0.5;
            latency_report.rotate_count += 0.5;
        }
        else
        {
            int rest_new_write_count = new_write_count - rest_blocks;
            int involved_tracks = rest_new_write_count / bottomSectorsPerTracks;
            seek_count++;
            latency_report.seek_count++;
            rotate_count += 0.5 + involved_tracks;
            latency_report.rotate_count += 0.5 + involved_tracks;
        }
        //write and mapping
        for (auto j = new_writes.begin(); j != new_writes.end(); j++)
        {
            // write_512_to_4k(*j, bottom_tracks_4k[write_pointer], 1); // valid data is represent with 1
            map_512_to_4k(*j, LtoP_map_4k, write_pointer, res_report);
            size_t bottom_track_number = write_pointer / bottomSectorsPerTracks;
            if (bottom_track_number >= bottom_track_status.size())
            {
                write_pointer++;
                continue;
            }
            bottom_track_status[bottom_track_number] = true;
            //move pointer
            write_pointer++;
        }
    }
    // Below deal with parity
    if (parity_track_counts > 0)
    {
        for (size_t j = 0; j < parity_track_counts; j++)
        {
            size_t top_track_number_first = parity_pointer / topSectorsPerTracks;
            size_t top_track_number_second = parity_pointer / topSectorsPerTracks + 1;
            if (top_track_number_second >= top_track_status.size())
            {
                break;
            }
            if (top_track_number_first < topTrackNumber)
            {
                top_track_status[top_track_number_first] = true;
                top_track_status[top_track_number_second] = true;
            }
            if (parity_pointer >= topSectorNumber)
            {  
                break;
            }
            else
            {
                parity_pointer += 2 * (unsigned __int64)topSectorsPerTracks; // forward the parity_pointer by 2 top tracks after writing one parity track
            }
        }
    }
    //count parity seek rotate
    seek_count += parity_track_counts;
    latency_report.seek_count += parity_track_counts;
    rotate_count += 1.5 * parity_track_counts;
    latency_report.rotate_count += 1.5 * parity_track_counts;
    //report latency
    double latency = latency_report.seek_count * seek_time + latency_report.rotate_count * rotate_time;
    res_report.total_latency = latency;
    return res_report;
}

void IMR_Baseline::write_512_to_4k(std::vector<unsigned __int64>& block_4k, int8_t* tracks, int element)
{
    for (int i = 0; i < block_4k.size(); i++)
    {
        try
        {
            tracks[i] = element;
        }
        catch (const std::exception& e)
        {
            std::cerr << "Not enough space for new writes" << "\n";
        }
    }
}

void IMR_Baseline::map_512_to_4k(std::vector<unsigned __int64>& block_4k, std::map<unsigned __int64, ENTRY_4K>& LtoP_map_4k, unsigned __int64& write_pointer, TOTAL_REPORT& res_report)
{
    for (int i = 0; i < block_4k.size(); i++)
    {
        LtoP_map_4k[block_4k[i]].block_num = write_pointer;
        LtoP_map_4k[block_4k[i]].offset = i;
        LtoP_map_4k[block_4k[i]].valid = true;
        res_report.mapping_entries_4k[block_4k[i]].block_num = write_pointer;
        res_report.mapping_entries_4k[block_4k[i]].offset = i;
        res_report.mapping_entries_4k[block_4k[i]].valid = true;
    }
}

TOTAL_REPORT IMR_Baseline::read_blocks_from_buffer(std::vector<unsigned __int64>& buffer_vec)
{
    LATENCY_REPORT latency_report;
    TOTAL_REPORT res_report;
    //LATENCY_REPORT initialization
    latency_report.seek_count = 0;
    latency_report.rotate_count = 0.0;
    res_report.total_latency = 0.0;
    if (buffer_vec.size() == 0)
    {
        std::cout << "Empty buffer vec happens\n";
        return res_report;
    }
    std::map<unsigned __int64, int> involved_bottom_tracks; //involved track number, involved times
    std::map<unsigned __int64, int> involved_top_tracks;
    for (auto i = buffer_vec.begin(); i != buffer_vec.end(); i++)
    {
        if (isInMap(*i))
        {
            unsigned __int64 p_address = LtoP_map[*i];
            //check if it is in bottom tracks or top tracks, if in bottom track...
            if (p_address < bottomSectorNumber)
            {
                //check track number and store the involved track number
                involved_bottom_tracks[p_address / bottomSectorsPerTracks]++;
            }
            else
            {
                involved_top_tracks[(p_address - bottomTrackNumber) / topSectorsPerTracks]++;
            }
        }
        else
        {
            std::cerr << "Read Not Found Due to Unsync between Global Map and Local Map\n";
        }
    }
    //make bottom track continuous vec
    std::vector<std::vector<unsigned __int64>> involved_continuous_bottom_tracks;
    std::vector<std::vector<unsigned __int64>> involved_continuous_top_tracks;
    buffer_to_continuous_vec(involved_bottom_tracks, involved_continuous_bottom_tracks);
    buffer_to_continuous_vec(involved_top_tracks, involved_continuous_top_tracks);
    // counts for bottom tracks

    for (auto i = involved_continuous_bottom_tracks.begin(); i != involved_continuous_bottom_tracks.end(); i++)
    {
        unsigned __int64 continuous_tracks_count = i->size();
        if (continuous_tracks_count > 0)
        {
            seek_count++;
            latency_report.seek_count++;
            rotate_count += 0.5 + (continuous_tracks_count - 1); //seek time + 0.5 rotate + miss rotations
            latency_report.rotate_count += 0.5;
        }
    }
    //counts for top tracks
    for (auto i = involved_continuous_top_tracks.begin(); i != involved_continuous_top_tracks.end(); i++)
    {
        unsigned __int64 continuous_tracks_count = i->size();
        if (continuous_tracks_count > 0)
        {
            seek_count++;
            latency_report.seek_count++;
            rotate_count += 0.5 + (continuous_tracks_count - 1); //seek time + 0.5 rotate + miss rotations
            latency_report.rotate_count += 0.5;
        }
    }
    //report latency
    double latency = latency_report.seek_count * seek_time + latency_report.rotate_count * rotate_time;
    res_report.total_latency = latency;
    return res_report;
}

TOTAL_REPORT IMR_Baseline::read_4k_from_buffer(std::vector<std::vector<unsigned __int64>>& buffer_vec)
{
    LATENCY_REPORT latency_report;
    TOTAL_REPORT res_report;
    //LATENCY_REPORT initialization
    latency_report.seek_count = 0;
    latency_report.rotate_count = 0.0;
    res_report.total_latency = 0.0;
    if (buffer_vec.size() == 0)
    {
        std::cout << "Empty buffer vec happens\n";
        return res_report;
    }
    std::map<unsigned __int64, int> involved_bottom_tracks; //involved track number, involved times
    std::map<unsigned __int64, int> involved_top_tracks;
    std::set<unsigned __int64> outplace_update_count;
    for (auto i = buffer_vec.begin(); i != buffer_vec.end(); i++)
    {
        if ((*i).size() == 0)
            continue;
        if (isInMap_4k((*i)[0]))
        {
            unsigned __int64 p_address = LtoP_map_4k[(*i)[0]].block_num;
            //check if it is in bottom tracks or top tracks, if in bottom track...
            if (LtoP_map_4k[(*i)[0]].valid)
            {
                //check track number and store the involved track number
                involved_bottom_tracks[p_address / bottomSectorsPerTracks]++;
            }
            else
            {
                // involved_top_tracks[(p_address - bottomTrackNumber) / topSectorsPerTracks]++;
                unsigned __int64 top_cache_num = p_address / ((raid_num_of_disks - 1) * bottomSectorsPerTracks);
                outplace_update_count.insert(top_cache_num);
            }
        }
        else
        {
            std::cerr << "Read Not Found Due to Unsync between Global Map and Local Map\n";
        }
    }
    //make bottom track continuous vec
    std::vector<std::vector<unsigned __int64>> involved_continuous_bottom_tracks;
    std::vector<std::vector<unsigned __int64>> involved_continuous_top_tracks;
    buffer_to_continuous_vec(involved_bottom_tracks, involved_continuous_bottom_tracks);
    buffer_to_continuous_vec(involved_top_tracks, involved_continuous_top_tracks);
    // counts for bottom tracks

    for (auto i = involved_continuous_bottom_tracks.begin(); i != involved_continuous_bottom_tracks.end(); i++)
    {
        unsigned __int64 continuous_tracks_count = i->size();
        if (continuous_tracks_count > 0)
        {
            seek_count++;
            latency_report.seek_count++;
            rotate_count += 0.5 + (continuous_tracks_count - 1); //seek time + 0.5 rotate + miss rotations
            latency_report.rotate_count += 0.5;
        }
    }
    //counts for top tracks
    /*for (auto i = involved_continuous_top_tracks.begin(); i != involved_continuous_top_tracks.end(); i++)
    {
        unsigned __int64 continuous_tracks_count = i->size();
        if (continuous_tracks_count > 0)
        {
            seek_count++;
            latency_report.seek_count++;
            rotate_count += 0.5 + (continuous_tracks_count - 1); //seek time + 0.5 rotate + miss rotations
            latency_report.rotate_count += 0.5;
        }
    }*/

    /*seek_count += involved_top_tracks.size();
    latency_report.seek_count += involved_top_tracks.size();
    rotate_count += 0.5 * involved_top_tracks.size(); //seek time + 0.5 rotate + miss rotations
    latency_report.rotate_count += 0.5 * involved_top_tracks.size();*/
    if (outplace_update_count.size() > 0)
    {
        seek_count += 1;
        latency_report.seek_count += 1;
        rotate_count += 0.5 * (double)outplace_update_count.size(); //seek time + 0.5 rotate + miss rotations
        latency_report.rotate_count += 0.5 * (double)outplace_update_count.size();
    }
    //report latency
    double latency = latency_report.seek_count * seek_time + latency_report.rotate_count * rotate_time;
    res_report.total_latency = latency;
    return res_report;
}

void IMR_Baseline::update_to_top_cache(std::vector<unsigned __int64> updates, TOTAL_REPORT &res_report, LATENCY_REPORT &latency_report)
{
    std::set<unsigned __int64> top_track_st;
    std::set<unsigned __int64> RMW_st;
    for (auto &i : updates)
    {
        ENTRY_4K entry = LtoP_map_4k[i];
        unsigned __int64 top_cache_num = entry.block_num / (bottomSectorsPerTracks * (unsigned __int64)(raid_num_of_disks - 1));
        size_t curr_write_pointer = top_cache_mapping_table[top_cache_num].top_cache_write_pointer;
        LtoP_map_4k[i].valid = false; // invalidate the original PBA
        // set mapping entry
        res_report.mapping_entries_4k[i].block_num = LtoP_map_4k[i].block_num;
        res_report.mapping_entries_4k[i].offset = LtoP_map_4k[i].offset;
        res_report.mapping_entries_4k[i].valid = false;

        TOP_CACHE_ENTRY top_cache_entry;
        top_cache_entry.original_p_addrerss = LtoP_map_4k[i].block_num;
        top_cache_entry.p_offset = curr_write_pointer;

        for (auto& j : top_cache_mapping_table[top_cache_num].LRU_map)
        {
            if (j.second.original_p_addrerss == LtoP_map_4k[i].block_num)
            {
                top_track_st.insert(top_cache_num);
                goto next;
            }
        }

        if (top_cache_mapping_table[top_cache_num].LRU_map.size() >= top_cache_block_num) // if top_cache_mapping_table is full
        {
            //evict the oldest one
            unsigned __int64 evicted_LBA = top_cache_mapping_table[top_cache_num].LRU_map.back().first;
            unsigned __int64 original_p_address = top_cache_mapping_table[top_cache_num].LRU_map.back().second.original_p_addrerss;
            top_cache_mapping_table[top_cache_num].LRU_map.pop_back();
            //place the data back to original p_address(actually only set to valid), and mapping. this will cause one RMW
            LtoP_map_4k[evicted_LBA].valid = true;
            res_report.mapping_entries_4k[evicted_LBA].valid = true;
            //evict all the data in the same bottom track
            std::deque<std::pair<unsigned __int64, TOP_CACHE_ENTRY>>::iterator it = top_cache_mapping_table[top_cache_num].LRU_map.begin();
            while (it != top_cache_mapping_table[top_cache_num].LRU_map.end())
            {
                if (it->second.original_p_addrerss / bottomSectorsPerTracks == original_p_address / bottomSectorsPerTracks)
                {
                    LtoP_map_4k[it->first].valid = true;
                    res_report.mapping_entries_4k[it->first].valid = true;
                    it = top_cache_mapping_table[top_cache_num].LRU_map.erase(it);
                }
                else
                {
                    ++it;
                }
            }
            RMW_st.insert(original_p_address / bottomSectorsPerTracks); // RMW tracks +1
            top_cache_mapping_table[top_cache_num].LRU_map.push_front(std::make_pair(i, top_cache_entry));// set for the top cache linked list
        }
        else
        {
            top_cache_mapping_table[top_cache_num].LRU_map.push_front(std::make_pair(i, top_cache_entry));// set for the top cache linked list
            top_cache_mapping_table[top_cache_num].top_cache_write_pointer++; // move the top_cache_write _pointer
            top_track_st.insert(top_cache_num);
        }

    next:
        continue;
    }
    //update top cache latency
    seek_count += top_track_st.size();
    latency_report.seek_count += top_track_st.size();
    rotate_count += 0.5 * top_track_st.size();
    latency_report.rotate_count += 0.5 * top_track_st.size();
    //RMW latency
    RMW_num += RMW_st.size();
    seek_count += RMW_st.size();
    latency_report.seek_count += RMW_st.size();
    rotate_count += 5.5 * RMW_st.size();
    latency_report.rotate_count += 5.5 * RMW_st.size();
}
