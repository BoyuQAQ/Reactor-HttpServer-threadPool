#ifndef _FAISS_WRAPPER_H_
#define _FAISS_WRAPPER_H_

#include <string>
#include <vector>

class FaissIndex {
public:
    FaissIndex();
    ~FaissIndex();

    bool init(int dimension, const std::string &index_path = "");
    
    bool add_vector(const float *vector, int id);
    
    std::vector<std::pair<int, float>> search(const float *query_vector, int top_k = 10, float threshold = 0.45);
    
    bool save(const std::string &index_path);
    
    bool load(const std::string &index_path);
    
    void reset();

private:
    int dimension_;
    std::vector<float> vectors_;
    std::vector<int> ids_;
    std::string index_path_;
    bool initialized_;
};

#endif
