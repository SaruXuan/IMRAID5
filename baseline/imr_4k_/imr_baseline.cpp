#include "imr.h"
#include <iostream>
#include <vector>
#include <fstream>
#include <string>
#include <map>
#include <cstdlib>
#include <set>

IMR_Baseline::IMR_Baseline(const unsigned __int64 size, const size_t block_size, const size_t block_per_bottom_track)
{
    totalBytes = size;
    sectorSize = block_size;
    write_count = 0;
    read_count = 0;
    seek_count = 0;
    rotate_count = 0.0;
    update_num = 0;
    RMW_num = 0;
    seek_time = 8;
    rotate_time = 5;
    bottomSectorsPerTracks = block_per_bottom_track;
    topSectorsPerTracks = bottomSectorsPerTracks * 3 / 4;

    totalSectorNumber = size / sectorSize;

    bottomSectorNumber = totalSectorNumber * 4 / 7;
    bottomTrackNumber = bottomSectorNumber / bottomSectorsPerTracks;

    topSectorNumber = totalSectorNumber * 3 / 7;
    topTrackNumber = topSectorNumber / topSectorsPerTracks;

    bottom_track_status.resize(bottomTrackNumber, false);
    top_track_status.resize(topTrackNumber, false);
    for (unsigned __int64 i = 0; i < bottomTrackNumber; i++)
    {
        bottom_track_status[i] = false;
    }
    for (unsigned __int64 i = 0; i < topTrackNumber; i++)
    {
        top_track_status[i] = false;
    }
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
        std::cout << "[" << it->first << "]: PBA " << it->second.block_num << " offset " << it->second.offset << "\n";
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
    if (top_track_left_number >= top_track_status.size())
        return true;
    if (p_address / bottomSectorsPerTracks == bottomTrackNumber - 1)
    {
        return top_track_status[top_track_left_number];
    }
    unsigned __int64 top_track_right_number = (p_address / bottomSectorsPerTracks) + 1;
    return top_track_status[top_track_left_number] || top_track_status[top_track_right_number];
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

//dynamic mapping
TOTAL_REPORT IMR_Baseline::write_blocks_from_buffer(std::vector<unsigned __int64>& buffer_vec)
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
    std::vector<unsigned __int64> new_writes;
    for (auto i = buffer_vec.begin(); i != buffer_vec.end(); i++)
    {
        if (isInMap(*i))
        {
            unsigned __int64 p_address = LtoP_map[*i];
            update_num++;
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
            new_writes.push_back(*i);
        }
    }
    //Below deal with updates
    //make bottom track continuous vec
    std::vector<std::vector<unsigned __int64>> involved_continuous_bottom_tracks;
    std::vector<std::vector<unsigned __int64>> involved_continuous_top_tracks;
    buffer_to_continuous_vec(involved_bottom_tracks, involved_continuous_bottom_tracks);
    buffer_to_continuous_vec(involved_top_tracks, involved_continuous_top_tracks);
    // counts for bottom tracks
    for (auto i = involved_continuous_bottom_tracks.begin(); i != involved_continuous_bottom_tracks.end(); i++)
    {
        unsigned __int64 continuous_uncovered_bottom_tracks_count = 0;
        for (auto j = i->begin(); j != i->end(); j++)
        {
            if (isNeighborTopTracksValid_by_track(*j))
            {
                RMW_num++;
                seek_count++;
                latency_report.seek_count++;
                rotate_count += 5.5;
                latency_report.rotate_count += 5.5;
            }
            else
            {
                //count uncovered continuous tracks
                continuous_uncovered_bottom_tracks_count++;
            }
        }
        if (continuous_uncovered_bottom_tracks_count > 0) //if there is uncovered tracks
        {
            seek_count++;
            latency_report.seek_count++;
            rotate_count += 0.5 + (continuous_uncovered_bottom_tracks_count - 1); //seek time + 0.5 rotate + miss rotations
            latency_report.rotate_count += 0.5;
        }
    }
    //counts for top tracks
    for (auto i = involved_continuous_top_tracks.begin(); i != involved_continuous_top_tracks.end(); i++)
    {
        unsigned __int64 continuous_top_tracks_count = i->size();
        seek_count++;
        latency_report.seek_count++;
        rotate_count += 0.5 + (continuous_top_tracks_count - 1);
        latency_report.rotate_count += 0.5 + (continuous_top_tracks_count - 1);
    }
    //Below deal with new writes
    //count seek rotation
    size_t new_write_count = new_writes.size();
    if (new_write_count > 0)
    {
        if (write_pointer < bottomSectorNumber)
        {
            size_t rest_blocks = bottomSectorsPerTracks - (write_pointer % bottomSectorsPerTracks) - 1;
            if (rest_blocks > new_write_count)
            {
                seek_count++;
                latency_report.seek_count++;
                rotate_count += 0.5;
                latency_report.rotate_count += 0.5;
            }
            else
            {
                size_t rest_new_write_count = new_write_count - rest_blocks;
                size_t involved_tracks = rest_new_write_count / bottomSectorsPerTracks;
                seek_count++;
                latency_report.seek_count++;
                rotate_count += 0.5 + involved_tracks;
                latency_report.rotate_count += 0.5 + involved_tracks;
            }
        }
        else
        {
            size_t rest_blocks = topSectorsPerTracks - (write_pointer % topSectorsPerTracks) - 1;
            if (rest_blocks > new_write_count)
            {
                seek_count++;
                latency_report.seek_count++;
                rotate_count += 0.5;
                latency_report.rotate_count += 0.5;
            }
            else
            {
                seek_count++;
                latency_report.seek_count++;
                size_t rest_new_write_count = new_write_count - rest_blocks;
                size_t involved_tracks = rest_new_write_count / topSectorsPerTracks;
                rotate_count += 0.5 + involved_tracks;
                latency_report.rotate_count += 0.5 + involved_tracks;
            }
        }
        //write and mapping
        for (auto i = new_writes.begin(); i != new_writes.end(); i++)
        {
            int element = (*i == (unsigned __int64)-1) ? -1 : 1;
            if (write_pointer < bottomSectorNumber)
            { // bottom tracks
                unsigned __int64 bottom_track_number = write_pointer / bottomSectorsPerTracks;
                if (bottom_track_number < 0)
                {
                    bottom_track_number = 0;
                }
                bottom_track_status[bottom_track_number] = true;
            }
            else
            { //top tracks
                unsigned __int64 top_track_number = (write_pointer - bottomSectorNumber) / topSectorsPerTracks;
                if (top_track_number < 0)
                {
                    top_track_number = 0;
                }
                top_track_status[top_track_number] = true;
            }
            //mapping
            if (element != -1)
            {
                LtoP_map[*i] = write_pointer;
                res_report.mapping_entries[*i] = write_pointer;
            }
            //move pointer
            write_pointer++;
        }
    }
    //report latency
    double latency = latency_report.seek_count * seek_time + latency_report.rotate_count * rotate_time;
    res_report.total_latency = latency;
    return res_report;
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
    std::set<unsigned __int64> update_tracks;
    for (auto i = buffer_vec_4k.begin(); i != buffer_vec_4k.end(); i++)
    {
        if ((*i).size() == 0)
            continue;
        if (isInMap_4k((*i)[0])) // update own its vector, so if update, there is only one lba in vector
        {
            unsigned __int64 p_address = LtoP_map_4k[(*i)[0]].block_num;
            unsigned __int64 offset = LtoP_map_4k[(*i)[0]].offset;
            update_num++;
            if (isNeighborTopTracksValid_by_P_address(p_address))
            {
                update_tracks.insert(p_address / bottomSectorsPerTracks);
            }
            else
            {
                seek_count++;
                latency_report.seek_count++;
                rotate_count += 0.5; //seek time + 0.5 rotate + miss rotations
                latency_report.rotate_count += 0.5;
            }
        }
        else
        {
            new_writes.push_back(*i);
        }
    }
    //Below deal with updates
    RMW_num += update_tracks.size();
    seek_count += update_tracks.size();
    latency_report.seek_count += update_tracks.size();
    rotate_count += 5.5 * update_tracks.size();
    latency_report.rotate_count += 5.5 * update_tracks.size();
    // add seek rotate count for parity
    RMW_num += update_tracks.size();
    seek_count += update_tracks.size();
    latency_report.seek_count += update_tracks.size();
    rotate_count += 5.5 * update_tracks.size();
    latency_report.rotate_count += 5.5 * update_tracks.size();
    
    //Below deal with new writes
    //count seek rotation
    size_t new_write_count = new_writes.size();
    if (new_write_count > 0)
    {
        if (write_pointer < bottomSectorNumber)
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
                size_t rest_new_write_count = new_write_count - rest_blocks;
                size_t involved_tracks = rest_new_write_count / bottomSectorsPerTracks;
                seek_count++;
                latency_report.seek_count++;
                rotate_count += 0.5 + involved_tracks;
                latency_report.rotate_count += 0.5 + involved_tracks;
            }
        }
        else
        {
            size_t rest_blocks = topSectorsPerTracks - ((write_pointer - bottomSectorNumber) % topSectorsPerTracks);
            if (rest_blocks > new_write_count)
            {
                seek_count++;
                latency_report.seek_count++;
                rotate_count += 0.5;
                latency_report.rotate_count += 0.5;
            }
            else
            {
                seek_count++;
                latency_report.seek_count++;
                size_t rest_new_write_count = new_write_count - rest_blocks;
                size_t involved_tracks = rest_new_write_count / topSectorsPerTracks;
                rotate_count += 0.5 + involved_tracks;
                latency_report.rotate_count += 0.5 + involved_tracks;
            }
        }
        //write and mapping
        for (auto i = new_writes.begin(); i != new_writes.end(); i++)
        {
            int element = ((*i)[0] == (unsigned __int64)-1) ? -1 : 1;
            if (write_pointer < bottomSectorNumber)
            {
                if (element != -1)
                    map_512_to_4k(*i, LtoP_map_4k, write_pointer, res_report);
                unsigned __int64 bottom_track_number = write_pointer / bottomSectorsPerTracks;
                if (bottom_track_number >= bottom_track_status.size())
                {
                    write_pointer++;
                    continue;
                }
                bottom_track_status[bottom_track_number] = true;
            }
            else
            {
                unsigned __int64 top_write_pointer = write_pointer - bottomSectorNumber;
                if (element != -1)
                    map_512_to_4k(*i, LtoP_map_4k, top_write_pointer, res_report);
                unsigned __int64 top_track_number = top_write_pointer / topSectorsPerTracks;
                if (top_track_number >= top_track_status.size())
                {
                    write_pointer++;
                    continue;
                }
                    
                top_track_status[top_track_number] = true;
            }
            //move pointer
            write_pointer++;
        }
    }
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
            std::cerr << e.what() << "\n";
        }
    }
}

void IMR_Baseline::map_512_to_4k(std::vector<unsigned __int64>& block_4k, std::map<unsigned __int64, ENTRY_4K>& LtoP_map_4k, unsigned __int64& write_pointer, TOTAL_REPORT& res_report)
{
    for (int i = 0; i < block_4k.size(); i++)
    {
        LtoP_map_4k[block_4k[i]].block_num = write_pointer;
        LtoP_map_4k[block_4k[i]].offset = i;
        res_report.mapping_entries_4k[block_4k[i]].block_num = write_pointer;
        res_report.mapping_entries_4k[block_4k[i]].offset = i;
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
    for (auto i = buffer_vec.begin(); i != buffer_vec.end(); i++)
    {
        if ((*i).size() == 0)
            continue;
        if (isInMap_4k((*i)[0]))
        {
            unsigned __int64 p_address = LtoP_map_4k[(*i)[0]].block_num;
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

void IMR_Baseline::write_block(unsigned __int64 LBA, int data)
{
    if (isInMap(LBA))
    {
        //if yes, get physical address
        unsigned __int64 p_address = LtoP_map[LBA];
        //check if it is in bottom tracks or top tracks, if in bottom track...
        if (p_address < bottomSectorNumber)
        {
            //check whether the neighboring top tracks have valid data
            if (isNeighborTopTracksValid_by_P_address(p_address))
            {
                //need RMW
                RMW_num++;
                //change mapping
                update_num++;
                //read count
                read_count += 2;
                //write count
                write_count += 3;
            }
            //if bottomtracks can be freely updated
            else
            {
                //change mapping
                update_num++;
                //write count
                write_count++;
            }
        }
        //if the update is in top tracks
        else
        {
            //change mapping
            update_num++;
            write_count++;
        }
        //mapping
    }
    //if the data is new
    else
    {
        //if no, write new data
        if (write_pointer < bottomSectorNumber)
        { // bottom tracks
            unsigned __int64 bottom_track_number = write_pointer / bottomSectorsPerTracks;
            bottom_track_status[bottom_track_number] = true;
        }
        else
        { //top tracks
            unsigned __int64 top_track_number = (write_pointer - bottomSectorNumber) / topSectorsPerTracks;
            top_track_status[top_track_number] = true;
        }
        //mapping
        if (data >= 0)
            LtoP_map[LBA] = write_pointer;
        //move pointer
        write_pointer++;
        //write count
        write_count++;
    }
}