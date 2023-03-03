#include <iostream>
#include <sys/time.h>

#include "ec/ECBase.hh"
#include "ec/Clay.hh"
#include "ec/RSCONV.hh"
#include "ec/RSPIPE.hh"

using namespace std;

void usage()
{
    cout << "Usage: ./Tester" << endl;
    cout << "    1. n" << endl;
    cout << "    2. k" << endl;
    cout << "    3. w" << endl;
    cout << "    4. pkt KiB" << endl;
    cout << "    5. block MiB" << endl;
    cout << "    6. repair index" << endl;
}

double getCurrentTime()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1e+6 + (double)tv.tv_usec;
}

int main(int argc, char **argv)
{

    if (argc != 7)
    {
        usage();
        return 0;
    }

    int n = atoi(argv[1]);      // 4
    int k = atoi(argv[2]);      // 2
    int w = atoi(argv[3]);      // 4 分包数量
    int pktKiB = atoi(argv[4]); // 1
    int pktbytes = pktKiB * 1024;
    int blkMiB = atoi(argv[5]); // 1
    int blkbytes = blkMiB * 1048576;
    int repairIdx = atoi(argv[6]); // 0 未知

    vector<string> param;

    // 一个ec,一个dec
    ECBase *ec = new Clay(n, k, w, {to_string(n - 1)});
    ECBase *dec = new Clay(n, k, w, {to_string(n - 1)});

    w = ec->_w;                 // w修正
    cout << "w: " << w << endl; // 即 cube的高为4

    int stripenum = blkbytes / pktbytes; // 1024 blocksize/packetsize = 一个block中的packet数量 = 条带中的数量
    int slicebytes = pktbytes / w;       //

    string stripename = "stripe0";
    vector<string> blklist;
    for (int i = 0; i < n; i++)
    {
        string blkname = "blk" + to_string(i);
        blklist.push_back(blkname);
    }

    // 0. prepare buffers
    char **buffers = (char **)calloc(n, sizeof(char *)); // 准备n个buffer
    for (int i = 0; i < n; i++)
    {
        // 每个buffer对应 stripe中的一个packet
        buffers[i] = (char *)calloc(pktbytes, sizeof(char));
        memset(buffers[i], 0, pktbytes);
        // 对于数据块, 填充cid,对于parity块,填充0
        if (i < k)
        {
            for (int j = 0; j < w; j++)
            {
                int cid = i * w + j;
                memset(buffers[i], cid, pktbytes);
            }
        }
    }
    char *repairbuffer = (char *)calloc(pktbytes, sizeof(char));

    // 1. prepare for encode
    ECDAG *encdag = nullptr;
    unordered_map<int, char *> encodeBufMap;

    // 1.1 create encode tasks
    // 这里的ec是clay,
    encdag = ec->Encode(); // 封装encdag
    cout << ">>> encode is finished" << endl;



    // split
    cout << endl <<  ">>> split is begining" << endl;
    vector<ECDAG *>* split_ecdags = encdag->split();
    cout << ">>> split is finished" << endl;

    // faltten
    cout << endl << ">>> flatten is begining" << endl;
    vector<ECDAG *> flatten_ecdags;
    cout << split_ecdags->size() << endl;
    for(ECDAG* dag: *split_ecdags){
        ECDAG* flatten = dag->flatten();
        flatten_ecdags.push_back(flatten);
    }
    cout << ">>> flatten is finished" << endl;
    

    encdag->genECUnits();

    // 1.2 generate ComputeTask from ECUnits
    vector<ComputeTask *> encctasklist;
    encdag->genComputeTaskByECUnits(encctasklist);

    // 1.3 put slices into encodeBufMap
    for (int i = 0; i < n; i++)
    {
        for (int j = 0; j < w; j++)
        {
            int idx = i * w + j;
            char *buf = buffers[i] + j * slicebytes;
            encodeBufMap.insert(make_pair(idx, buf));
        }
    }

    // 1.4 put shortened pkts into encodeBufMap
    vector<int> encHeaders = encdag->getECLeaves();
    for (auto cid : encHeaders)
    {
        if (encodeBufMap.find(cid) == encodeBufMap.end())
        { // 如果没有找到
            char *tmpbuf = (char *)calloc(pktbytes / w, sizeof(char));
            encodeBufMap.insert(make_pair(cid, tmpbuf));
        }
    }

    // 2. prepare for decode
    ECDAG *decdag = nullptr;
    unordered_map<int, char *> decodeBufMap;

    // 2.1 create decode tasks
    vector<int> availIdx;
    vector<int> toRepairIdx;
    for (int i = 0; i < n; i++)
    {
        for (int j = 0; j < w; j++)
        {
            int idx = i * w + j;
            if (i == repairIdx)
            {
                toRepairIdx.push_back(idx);
                char *buf = repairbuffer + j * slicebytes;
                decodeBufMap.insert(make_pair(idx, buf));
            }
            else
            {
                availIdx.push_back(idx);
                char *buf = buffers[i] + j * slicebytes;
                decodeBufMap.insert(make_pair(idx, buf));
            }
        }
    }
    decdag = dec->Decode(availIdx, toRepairIdx);

    // decdag->Concact(toRepairIdx);
    decdag->genECUnits();

    // 2.2 generate ComputeTask from ECUnits
    vector<ComputeTask *> decctasklist;
    decdag->genComputeTaskByECUnits(decctasklist);

    // 2.3 put shortened pkts into encodeBufMap
    vector<int> decHeaders = decdag->getECLeaves();
    for (auto cid : decHeaders)
    {
        if (decodeBufMap.find(cid) == decodeBufMap.end())
        {
            char *tmpbuf = (char *)calloc(pktbytes / w, sizeof(char));
            decodeBufMap.insert(make_pair(cid, tmpbuf));
        }
    }

    // 2. test
    double encodeTime = 0, decodeTime = 0;
    srand((unsigned)1234);
    cout << "stripenum: " << stripenum << endl;
    for (int stripei = 0; stripei < stripenum; stripei++)
    {
        // clean codebuffers
        for (int i = k; i < n; i++)
        {
            memset(buffers[i], 0, pktbytes);
        }

        //// initialize databuffers
        // for (int i=0; i<k; i++) {
        //     for (int j=0; j<pktbytes; j++) {
        //         buffers[i][j] = rand();
        //     }
        // }

        // debug
        for (int i = 0; i < k; i++)
        {
            for (int j = 0; j < w; j++)
            {
                char c = i * w + j;
                memset(buffers[i] + j * slicebytes, c, slicebytes);
            }
        }

        // encode test
        encodeTime -= getCurrentTime();
        for (int taskid = 0; taskid < encctasklist.size(); taskid++)
        {
            ComputeTask *cptask = encctasklist[taskid];
            vector<int> srclist = cptask->_srclist;
            vector<int> dstlist = cptask->_dstlist;
            vector<vector<int>> coefs = cptask->_coefs;

            // cout << "srclist: ";
            // for (int j=0; j<srclist.size(); j++)
            //   cout << srclist[j] << " ";
            // cout << endl;
            // for (int j=0; j<dstlist.size(); j++) {
            //   int target = dstlist[j];
            //   vector<int> coef = coefs[j];
            //   cout << "target: " << target << "; coef: ";
            //   for (int ci=0; ci<coef.size(); ci++) {
            //     cout << coef[ci] << " ";
            //   }
            //   cout << endl;
            // }

            // now we create buf in bufMap
            for (auto dstidx : dstlist)
            {
                if (encodeBufMap.find(dstidx) == encodeBufMap.end())
                {
                    char *tmpbuf = (char *)calloc(slicebytes, sizeof(char));
                    memset(tmpbuf, 0, slicebytes);
                    encodeBufMap.insert(make_pair(dstidx, tmpbuf));
                }
            }

            int col = srclist.size();
            int row = dstlist.size();
            int *matrix = (int *)calloc(row * col, sizeof(int));
            char **data = (char **)calloc(col, sizeof(char *));
            char **code = (char **)calloc(row, sizeof(char *));
            // prepare data buf
            for (int bufIdx = 0; bufIdx < srclist.size(); bufIdx++)
            {
                int child = srclist[bufIdx];
                // check whether there is buf in databuf
                assert(encodeBufMap.find(child) != encodeBufMap.end());
                data[bufIdx] = encodeBufMap[child];
                unsigned char c = data[bufIdx][0];
            }
            // prepare code buf and matrix
            for (int codeBufIdx = 0; codeBufIdx < dstlist.size(); codeBufIdx++)
            {
                int target = dstlist[codeBufIdx];
                char *codebuf;
                assert(encodeBufMap.find(target) != encodeBufMap.end());
                code[codeBufIdx] = encodeBufMap[target];
                vector<int> curcoef = coefs[codeBufIdx];
                for (int j = 0; j < col; j++)
                {
                    matrix[codeBufIdx * col + j] = curcoef[j];
                }
            }
            // perform computation
            Computation::Multi(code, data, matrix, row, col, slicebytes, "Isal");

            // cout << "srclist: ";
            // for (int j=0; j<srclist.size(); j++)
            //   cout << srclist[j] << " ";
            // cout << endl;
            // for (int j=0; j<dstlist.size(); j++) {
            //   int target = dstlist[j];
            //   vector<int> coef = coefs[j];
            //   cout << "target: " << target << ", value: " << (int)code[j][0] << "; coef: ";
            //   for (int ci=0; ci<coef.size(); ci++) {
            //     cout << coef[ci] << " ";
            //   }
            //   cout << endl;
            // }
            free(matrix);
            free(data);
            free(code);
        }

        encodeTime += getCurrentTime();

        // // debug encode
        // cout << "after encoding:" << endl;
        // for (int i=0; i<n; i++) {
        //     cout << "block[" << i << "]: ";
        //     for (int j=0; j<w; j++) {
        //         char c = buffers[i][j*slicebytes];
        //         cout << (int)c << " ";
        //     }
        //     cout << endl;
        // }

        // // debug decode
        // cout << "before decoding:" << endl;
        // for (int i=0; i<n; i++) {
        //     cout << "block[" << i << "]: ";
        //     for (int j=0; j<w; j++) {
        //         int idx = i*w+j;
        //         char* buf = decodeBufMap[idx];
        //         char c = buf[0];
        //         cout << (int)c << " ";

        //         for (int l=1; l<slicebytes; l++) {
        //             if (buf[l] != buf[l-1]) {
        //                 cout << " error at offset " << l << " ";
        //             }
        //         }
        //     }
        //     cout << endl;
        // }

        // decode test
        decodeTime -= getCurrentTime();
        for (int taskid = 0; taskid < decctasklist.size(); taskid++)
        {
            ComputeTask *cptask = decctasklist[taskid];
            vector<int> srclist = cptask->_srclist;
            vector<int> dstlist = cptask->_dstlist;
            vector<vector<int>> coefs = cptask->_coefs;

            // now we create buf in bufMap
            for (auto dstidx : dstlist)
            {
                if (decodeBufMap.find(dstidx) == decodeBufMap.end())
                {
                    char *tmpbuf = (char *)calloc(slicebytes, sizeof(char));
                    memset(tmpbuf, 0, slicebytes);
                    decodeBufMap.insert(make_pair(dstidx, tmpbuf));
                }
            }

            int col = srclist.size();
            int row = dstlist.size();
            int *matrix = (int *)calloc(row * col, sizeof(int));
            char **data = (char **)calloc(col, sizeof(char *));
            char **code = (char **)calloc(row, sizeof(char *));
            // prepare data buf
            for (int bufIdx = 0; bufIdx < srclist.size(); bufIdx++)
            {
                int child = srclist[bufIdx];
                // check whether there is buf in databuf
                assert(decodeBufMap.find(child) != decodeBufMap.end());
                data[bufIdx] = decodeBufMap[child];
                unsigned char c = data[bufIdx][0];
            }
            // prepare code buf and matrix
            for (int codeBufIdx = 0; codeBufIdx < dstlist.size(); codeBufIdx++)
            {
                int target = dstlist[codeBufIdx];
                char *codebuf;
                assert(decodeBufMap.find(target) != decodeBufMap.end());
                code[codeBufIdx] = decodeBufMap[target];
                vector<int> curcoef = coefs[codeBufIdx];
                for (int j = 0; j < col; j++)
                {
                    matrix[codeBufIdx * col + j] = curcoef[j];
                }
            }
            // perform computation
            Computation::Multi(code, data, matrix, row, col, slicebytes, "Isal");

            // cout << "srclist: ";
            // for (int j=0; j<srclist.size(); j++)
            //   cout << srclist[j] << " ";
            // cout << endl;
            // for (int j=0; j<dstlist.size(); j++) {
            //   int target = dstlist[j];
            //   vector<int> coef = coefs[j];
            //   cout << "target: " << target << ", value: " << (int)code[j][0] << "; coef: ";
            //   for (int ci=0; ci<coef.size(); ci++) {
            //     cout << coef[ci] << " ";
            //   }
            //   cout << endl;
            // }

            free(matrix);
            free(data);
            free(code);
        }
        decodeTime += getCurrentTime();

        // check correcness
        bool success = true;
        for (int i = 0; i < pktbytes; i++)
        {
            if (buffers[repairIdx][i] != repairbuffer[i])
            {
                success = false;
                cout << "repair error at offset: " << i << endl;
                break;
            }
        }

        if (!success)
        {
            cout << "repair error!" << endl;
            break;
        }
    }

    cout << "Encode Time: " << encodeTime / 1000 << " ms" << endl;
    cout << "Encode Trpt: " << blkbytes * k / 1.048576 / encodeTime << endl;
    cout << "Decode Time: " << decodeTime / 1000 << " ms" << endl;
    cout << "Decode Trpt: " << blkbytes / 1.048576 / encodeTime << endl;

    return 0;
}
