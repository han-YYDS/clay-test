#ifndef _ECDAG_HH_
#define _ECDAG_HH_

#include "../inc/include.hh"
#include "ECNode.hh"
#include "ECUnit.hh"
#include "../util/BlockingQueue.hh"

#include "Computation.hh"

using namespace std;

#define ECDAG_DEBUG_ENABLE true

#define REQUESTOR 32767
#define SGSTART 0
#define USTART 0
#define CSTART 0

class ComputeTask {
  public:
    vector<int> _srclist; // from
    vector<int> _dstlist; // to
    vector<vector<int>> _coefs; // 

    ComputeTask(vector<int> srclist, vector<int> dstlist, vector<vector<int>> coefs) {
      _srclist = srclist;
      _dstlist = dstlist;
      _coefs = coefs;
    }
    ~ComputeTask() {
    }
};

class MyHash
{
public:
    std::size_t operator()(const vector<pair<int,int>> &v) const
    {
        std::size_t x = 0;

        for (auto &i : v)
            x ^= std::hash<int>()(i.first) ^ std::hash<int>()(i.second);

        return x;
    }
};


class ECDAG {
  private:
    unordered_map<int, ECNode*> _ecNodeMap; // ecNode的id 和 node实例 的映射
    vector<int> _ecHeaders; // parent 
    vector<int> _ecLeaves; // child 

    // for ECUnits
    int _unitId = USTART;
    unordered_map<int, ECUnit*> _ecUnitMap;
    vector<int> _ecUnitList;

  public:
    ECDAG(); 
    ~ECDAG();

    void Join(int pidx, vector<int> cidx, vector<int> coefs); // 编码
    void Join_1(int pidx, vector<int> cidx, vector<int> coefs);
    void Concact(vector<int> cidx);
    ECNode*  getNode(int id);


    vector<ECDAG*>* split();
    ECDAG* split_1(vector<int> repair_idx);
    ECDAG* flatten();

    void genECUnits();
    void genComputeTaskByECUnits(vector<ComputeTask*>& tasklist);
    vector<int> getECLeaves();
    vector<int> getECHeaders();

    // for debug
    void dump(string);
};
#endif
