/* MIT License
 *
 * Copyright (c) 2018 Sam Kovaka <skovaka@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef ALIGNER_HPP
#define ALIGNER_HPP

#include <iostream>
#include <vector>
#include "bwa_fmi.hpp"
#include "kmer_model.hpp"
#include "normalizer.hpp"
#include "seed_tracker.hpp"
#include "fast5_reader.hpp"
#include "timer.hpp"

#define PAF_TIME_TAG "YT:f:"

//#define DEBUG_TIME
//#define DEBUG_SEEDS

#ifdef DEBUG_TIME
#define PAF_SIGPROC_TAG "YA:f:"
#define PAF_PROB_TAG   "YB:f:"
#define PAF_THRESH_TAG "YC:f:"
#define PAF_STAY_TAG   "YD:f:"
#define PAF_NEIGHBOR_TAG "YE:f:"
#define PAF_FMRS_TAG    "YF:f:"
#define PAF_FMSA_TAG    "YG:f:"
#define PAF_SORT_TAG    "YH:f:"
#define PAF_LOOP2_TAG   "YI:f:"
#define PAF_SOURCE_TAG  "YJ:f:"
#define PAF_TRACKER_TAG "YK:f:"
#endif

#define INDEX_SUFF ".uncl"

class MapperParams {
    public:
    MapperParams(const std::string &bwa_prefix,
                 const std::string &model_fname,
                 u32 seed_len, 
                 u32 min_aln_len,
                 u32 min_rep_len, 
                 u32 max_rep_copy, 
                 u32 max_consec_stay,
                 u32 max_paths, 
                 u32 max_events_proc,
                 u32 evt_buffer_len,
                 u32 evt_winlen1,
                 u32 evt_winlen2,
                 u16 evt_batch_size,
                 float evt_thresh1,
                 float evt_thresh2,
                 float evt_peak_height,
                 float evt_min_mean,
                 float evt_max_mean,
                 float max_stay_frac,
                 float min_seed_prob, 
                 float min_mean_conf,
                 float min_top_conf);
    
    u16 get_max_events(u16 event_i) const;
    float get_prob_thresh(u64 fm_length) const;
    float get_source_prob() const;

    BwaFMI fmi_;
    KmerModel model_;
    EventParams event_params_;

    u32 seed_len_,
        min_rep_len_,
        max_rep_copy_,
        max_paths_,
        max_consec_stay_,
        min_aln_len_,
        max_events_proc_,
        evt_buffer_len_;

    u16 evt_batch_size_;

    float max_stay_frac_,
          min_seed_prob_,
          min_mean_conf_,
          min_top_conf_;

    std::vector<u64> evpr_lengths_;
    std::vector<float> evpr_threshes_;
    std::vector<Range> kmer_fmranges_;
};

class ReadLoc {
    public:
    ReadLoc();
    ReadLoc(const std::string &rd_name, u16 channel=0, u32 number=0);

    bool set_ref_loc(const MapperParams &params, const SeedGroup &seeds);
    void set_read_len(const MapperParams &params, u32 len);

    void set_time(float time);

    std::string str() const;
    bool is_valid() const; 
    u16 get_channel() const;
    u32 get_number() const;

    friend std::ostream &operator<< (std::ostream &out, const ReadLoc &l);

    #ifdef DEBUG_TIME
    double sigproc_time_, prob_time_, thresh_time_, stay_time_,
           neighbor_time_, fmrs_time_, fmsa_time_, 
           sort_time_, loop2_time_, source_time_, tracker_time_;
    #endif

    private:
    std::string rd_name_, rf_name_;
    u16 rd_channel_;
    u32 rd_number_;
    u64 rd_st_, rd_en_, rd_len_,
        rf_st_, rf_en_, rf_len_;
    u16 match_count_;
    float time_;
    bool fwd_;

};

std::ostream &operator<< (std::ostream &out, const ReadLoc &l);

class Mapper {
    public:

    enum State { INACTIVE, MAPPING, SUCCESS, FAILURE };

    Mapper(const MapperParams &map_params, u16 channel);
    Mapper(const Mapper &m);

    ~Mapper();

    void new_read(const std::string &name, u32 number=0);

    std::string map_fast5(const std::string &fast5_name);
    ReadLoc add_samples(const std::vector<float> &samples);
    bool add_sample(float s);

    //bool swap_chunk(std::vector<float> &chunk);
    bool swap_chunk(Chunk &chunk);
    u16 process_chunk();
    bool map_chunk();
    bool is_chunk_processed() const;
    State get_state() const;

    u32 prev_unfinished(u32 next_number) const;

    bool finished() const;
    ReadLoc get_loc() const;
    ReadLoc pop_loc();

    private:

    enum EventType { MATCH, STAY, NUM_TYPES };
    static const u8 TYPE_BITS = 1;

    class PathBuffer {
        public:
        PathBuffer();
        PathBuffer(const PathBuffer &p);

        void make_source(Range &range, 
                         u16 kmer, 
                         float prob);

        void make_child(PathBuffer &p, 
                        Range &range, 
                        u16 kmer, 
                        float prob, 
                        EventType type);

        void invalidate();
        bool is_valid() const;
        bool is_seed_valid(const MapperParams &params, 
                           bool has_children) const;

        u8 type_head() const;
        u8 type_tail() const;
        u8 match_len() const;

        void free_buffers();
        void print() const;

        static u8 MAX_PATH_LEN, TYPE_MASK;
        static u32 TYPE_ADDS[EventType::NUM_TYPES];

        Range fm_range_;
        u8 length_,
            consec_stays_;
        u16 kmer_;

        float seed_prob_;
        float *prob_sums_;

        u32 event_types_;
        u8 path_type_counts_[EventType::NUM_TYPES];

        bool sa_checked_;
    };

    friend bool operator< (const PathBuffer &p1, const PathBuffer &p2);

    private:

    bool add_event(float event);

    void update_seeds(PathBuffer &p, bool has_children);

    const MapperParams &params_;
    const KmerModel &model_;
    const BwaFMI &fmi_;
    EventDetector event_detector_;
    Normalizer norm_;
    SeedTracker seed_tracker_;

    u16 channel_;
    u32 read_num_;
    bool chunk_processed_;
    State state_;
    std::vector<float> chunk_buffer_, kmer_probs_;
    std::vector<PathBuffer> prev_paths_, next_paths_;
    std::vector<bool> sources_added_;
    u32 prev_size_,
        event_i_,
        chunk_i_;
    Timer timer_;
    ReadLoc read_loc_;

    #ifdef DEBUG_SEEDS
    std::ostream &seeds_out_;
    #endif
};


#endif
