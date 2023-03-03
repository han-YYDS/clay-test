#include "ECDAG.hh"

ECDAG::ECDAG()
{
}

ECDAG::~ECDAG()
{
    for (auto item : _ecUnitMap)
        delete item.second;
    for (auto it : _ecNodeMap)
    {
        if (it.second)
        {
            delete it.second;
            it.second = nullptr;
        }
    }
    _ecNodeMap.clear();
}

void ECDAG::Join(int pidx, vector<int> cidx, vector<int> coefs)
{
    // debug start
    string msg = "ECDAG::Join(" + to_string(pidx) + ",";
    for (int i = 0; i < cidx.size(); i++)
        msg += " " + to_string(cidx[i]);
    msg += ",";
    for (int i = 0; i < coefs.size(); i++)
        msg += " " + to_string(coefs[i]);
    msg += ")";
    if (ECDAG_DEBUG_ENABLE)
        cout << msg << endl;
    // debug end

    // 0. deal with childs
    vector<ECNode *> targetChilds; // 对于该Join操作中的 childs

    // 设置 flags
    for (int i = 0; i < cidx.size(); i++)
    { // child
        int curId = cidx[i];
        // 0.0 check whether child exists in our ecNodeMap
        unordered_map<int, ECNode *>::const_iterator findNode = _ecNodeMap.find(curId);
        ECNode *curNode;
        if (findNode == _ecNodeMap.end())
        {
            // 如果该child结点为新加入的结点,需要对其进行构造,并在dag中进行记录
            // child does not exists, we need to create a new one
            curNode = new ECNode(curId);
            // a new child is set to leaf on creation
            curNode->setType("leaf");
            // insert into ecNodeMap
            _ecNodeMap.insert(make_pair(curId, curNode));
            _ecLeaves.push_back(curId);
        }
        else
        {
            // child exists
            curNode = _ecNodeMap[curId];
            // NOTE: if this node is marked with root before, it should be marked as intermediate now
            vector<int>::iterator findpos = find(_ecHeaders.begin(), _ecHeaders.end(), curId);
            if (findpos != _ecHeaders.end())
            {
                // delete from headers
                _ecHeaders.erase(findpos);
                curNode->setType("intermediate");
            }
        }
        // 0.1 add curNode into targetChilds
        targetChilds.push_back(curNode);
        //    // 0.3 increase refNo for curNode
        //    curNode->incRefNumFor(curId);
    }

    // 1. deal with parent
    ECNode *rNode;
    assert(_ecNodeMap.find(pidx) == _ecNodeMap.end()); // 这条断言限制了Join操作是从child向parent方向发展的
    // pidx does not exists, create new one and add to headers
    rNode = new ECNode(pidx);
    // parent node is set to root on creation
    rNode->setType("root");
    _ecNodeMap.insert(make_pair(pidx, rNode));
    _ecHeaders.push_back(pidx);

    // set child nodes for the root node, as well as the coefs
    rNode->setChilds(targetChilds); // 在Join中设置此次的
    rNode->setCoefs(coefs);
    //  rNode->addCoefs(pidx, coefs);

    // set parent node for each child node
    for (auto cnode : targetChilds)
    {
        cnode->addParentNode(rNode);
    }
}

// void ECDAG::Concact(vector<int> cidx) {
//   vector<int> coefs;
//   for (int i=0; i<cidx.size(); i++) {
//     coefs.push_back(-1);
//   }
//
//   Join(REQUESTOR, cidx, coefs);
// }

void ECDAG::genECUnits()
{
    // 0. generate inrefs and outrefs for each node
    unordered_map<int, int> inref;
    unordered_map<int, int> outref;
    for (auto item : _ecNodeMap)
    {
        int nodeId = item.first;
        ECNode *node = item.second;
        vector<ECNode *> childlist = node->getChildNodes();
        // increase outref for each child by 1
        for (auto cnode : childlist)
        {
            int cidx = cnode->getNodeId();
            if (outref.find(cidx) == outref.end())
                outref.insert(make_pair(cidx, 1));
            else
                outref[cidx]++;
        }
        // increate inref for current node by childlist.size();
        inref.insert(make_pair(nodeId, childlist.size()));
    }

    //  cout << "ECDAG::genECUnits.inref:" << endl;
    //  for (auto item: inref) {
    //    cout <<   item.first << ", " << item.second << endl;
    //  }
    //  cout << "ECDAG::genECUnits.outref:" << endl;
    //  for (auto item: outref) {
    //    cout <<   item.first << ", " << item.second << endl;
    //  }

    // 1. Each time we search for leaf nodes
    BlockingQueue<int> leaves;
    for (auto item : inref)
    {
        if (item.second == 0)
            leaves.push(item.first);
    }

    int debugnum = 0;
    while (leaves.getSize() > 0)
    {
        int nodeId = leaves.pop();
        // cout << "Check node "    << nodeId << endl;

        // if the outref of this node is zero, skip
        if (outref[nodeId] == 0)
        {
            // cout << "  outref[" << nodeId << "] = 0, skip" << endl;
            continue;
        }

        // verify parent node for curnode
        ECNode *curNode = _ecNodeMap[nodeId];
        vector<ECNode *> parentNodes = curNode->getParentNodes();
        if (parentNodes.size() == 0)
        {
            // cout << "Reach the root, exit!"  << endl;
            break;
        }

        for (auto pitem : parentNodes)
        {
            int pnodeId = pitem->getNodeId();
            ECNode *pnode = pitem;
            // cout << "  pnodeId: " << pnodeId << endl;
            //  figure out all the childnode for the current node
            vector<ECNode *> tmpChildList = pnode->getChildNodes();
            vector<int> tmpChildIndices = pnode->getChildIndices();
            vector<int> tmpcoefs = pnode->getCoefs();

            bool allleaves = true;
            bool finished = false;
            for (int tmpi = 0; tmpi < tmpChildIndices.size(); tmpi++)
            {
                int tmpcnodeId = tmpChildIndices[tmpi];
                int tmpinref = inref[tmpcnodeId];
                int tmpoutref = outref[tmpcnodeId];
                // cout << "    cnodeId: " << tmpcnodeId << ", inref: " << tmpinref << ", outref: " << tmpoutref << endl;
                if (tmpinref != 0)
                    allleaves = false;
                if (tmpoutref == 0)
                    finished = true;
            }

            if (!finished & allleaves)
            {
                // generate ECUnit
                ECUnit *ecunit = new ECUnit(_unitId++, tmpChildIndices, pnodeId, tmpcoefs);
                _ecUnitMap.insert(make_pair(ecunit->getUnitId(), ecunit));
                _ecUnitList.push_back(ecunit->getUnitId());

                // decrease child outref by 1
                for (int tmpi = 0; tmpi < tmpChildIndices.size(); tmpi++)
                {
                    int tmpcnodeId = tmpChildIndices[tmpi];
                    outref[tmpcnodeId]--;
                }
                // decrease parent inref by childsize
                inref[pnodeId] -= tmpChildIndices.size();
                if (inref[pnodeId] == 0)
                    leaves.push(pnodeId);
            }
        }

        // after checking all the parent of this node
        if (outref[nodeId] != 0)
            leaves.push(nodeId);

        // debugnum++;
        // if (debugnum > 2)
        //   break;
    }

    // debug
    for (int i = 0; i < _ecUnitList.size(); i++)
    {
        cout << _ecUnitMap[_ecUnitList[i]]->dump() << endl;
    }
}

void ECDAG::genComputeTaskByECUnits(vector<ComputeTask *> &tasklist)
{
    for (int i = 0; i < _ecUnitList.size(); i++)
    {
        int unitIdx = _ecUnitList[i];
        ECUnit *cunit = _ecUnitMap[unitIdx];

        vector<int> srclist = cunit->getChilds();
        int parent = cunit->getParent();
        if (parent == REQUESTOR)
            continue;
        vector<int> coef = cunit->getCoefs();

        vector<int> dstlist = {parent};
        vector<vector<int>> coeflist;
        coeflist.push_back(coef);

        ComputeTask *ct = new ComputeTask(srclist, dstlist, coeflist);
        tasklist.push_back(ct);
    }
}

vector<int> ECDAG::getECLeaves()
{
    return _ecLeaves;
}

vector<int> ECDAG::getECHeaders()
{
    return _ecHeaders;
}

void ECDAG::dump()
{
    for (auto id : _ecHeaders)
    {
        _ecNodeMap[id]->dump(-1);
        cout << endl;
    }
}

/**
 * @brief 对多层的,产生virtual中间块的ECDAG进行扁平化处理, 压缩成只有两层的dag
 *  只考虑single_failure的ECDAG,且该DAG是已被split过的,所以leaves可以直接使用
 *
 * @return ECDAG*
 */
ECDAG *ECDAG::flatten()
{
    assert(_ecHeaders.size() == 1); // single_failure
    ECDAG *flatten_ecdag = new ECDAG();
    int header_idx = _ecHeaders.back();
    cout << endl <<  "[DEBUG] split_header = " << header_idx << endl;

    ECNode *header_node = this->_ecNodeMap[header_idx];
    // unordered_map<int,int,MyHash> coef_map; // 从root到该节点的coef
    map<int, int> coef_map; // 从root到该节点的coef
    coef_map.insert(make_pair(header_idx, 1));
    deque<ECNode *> curr_nodes = {header_node};

    while (curr_nodes.size() != 0)
    {
        ECNode *curr_node = curr_nodes.front();
        int curr_coef = coef_map[curr_node->getNodeId()]; // 得到当前节点的coef 当前节点是已经记录了coef的
        int curr_idx = curr_node->getNodeId();
        if (curr_node->getType() != 0)
        {
            // 非child节点将其childs纳入curr_node
            vector<ECNode *> childs_nodes = curr_node->getChildNodes();
            vector<int> child_idxs = curr_node->getChildIndices();
            vector<int> curr_coefs = curr_node->getCoefs();
            
            // cout << "curr_node = "<< curr_idx<<  " curr_coef = " << curr_coef << endl;;
            // cout << "child_idxs = [";
            // for(auto it : child_idxs){
            //     cout << " " << it;
            // }       
            // cout << "]" <<endl;     
            
            // cout << "curr_coefs = [";
            // for(auto it : curr_coefs){
            //     cout << " " << it;
            // }       
            // cout << "]" << endl;

            for (int i = 0; i < child_idxs.size(); i++)
            {
                // cout << "[DEBUG] coef_map.insert: id = " << child_idxs[i] << " coef = " <<  curr_coefs[i] * curr_coef << endl;  
                coef_map.insert(make_pair(child_idxs[i], Computation::singleMulti(curr_coefs[i],curr_coef,8)));

                // cout << "[MULTI DBUG] " << curr_coefs[i] << " * " << curr_coef << " = " << curr_coefs[i] * curr_coef << endl; 
                // cout << "[MULTI DBUG] " << curr_coefs[i] << " multiply " << curr_coef << " = " << Computation::singleMulti(curr_coefs[i],curr_coef,8) << endl ;

                ECNode* child = childs_nodes[i];
                curr_nodes.push_back(child);
            }
        }
        // child节点 或 已经将childs纳入curr的节点 会被弹出
        curr_nodes.pop_front();
    }

    vector<int> leaves = this->getECLeaves();
    vector<int> coefs;
    for (auto leave : leaves)
    {
        coefs.push_back(coef_map[leave]);
    }
    flatten_ecdag->Join(header_idx, leaves, coefs);
    return flatten_ecdag;
}

/**
 * @brief 对该ecdag进行split, 拆分成只有单个ecHeader的dag, 目前不考虑多个sub_packet的情况
 *      // 记得解决 函数中所new的ecdag的释放问题
 * @return ECDAG*
 */
vector<ECDAG *>* ECDAG::split()
{
    // 对于每一个ecHeader,生成一个dag
    cout << "[DEBUG] split_ecdags._ecHeaders.size() = " << _ecHeaders.size();
    vector<ECDAG *>* split_ecdags = new vector<ECDAG*>();
    for (int header : _ecHeaders)
    {
        ECDAG *split_ecdag = new ECDAG();

        ECNode *header_node = _ecNodeMap[header];
        cout << endl
             << "[DEBUG] 当前header节点: " << header_node->getNodeId() << endl;
        deque<ECNode *> curr_nodes = {header_node};
        // 由于join操作只能由leaf向root发展,所以join命令弹栈
        deque<pair<int, pair<vector<int>, vector<int>>>> reverse_orders;
        while (curr_nodes.size() != 0)
        {
            ECNode *curr_node = curr_nodes.back();
            int curr_idx = curr_node->getNodeId();
            if (curr_node->getType() != 0)
            { // 非child节点将其childs纳入curr_node
                vector<ECNode *> childs_nodes = curr_node->getChildNodes();
                vector<int> child_idxs = curr_node->getChildIndices();
                vector<int> curr_coefs = curr_node->getCoefs();
                // split_ecdag->Join(curr_idx, child_idxs, curr_coefs);
                reverse_orders.push_back(make_pair(curr_idx, make_pair(child_idxs, curr_coefs)));
                for (auto child : childs_nodes)
                { // 将child纳入deque中
                    curr_nodes.push_front(child);
                }
            }
            // child节点 或 已经将childs纳入curr的节点 会被弹出
            curr_nodes.pop_back();
        }
        // 逆序执行命令
        while (!reverse_orders.empty())
        {
            pair<int, pair<vector<int>, vector<int>>> order = reverse_orders.back();
            reverse_orders.pop_back();
            split_ecdag->Join(order.first, order.second.first, order.second.second);
        }
        split_ecdags->push_back(split_ecdag);
    }
    return split_ecdags;
}
