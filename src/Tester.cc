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
    // cout << "    6. repair index" << endl;
    // cout << "    6. repair sub_packet amount" << endl;
}

double getCurrentTime()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1e+6 + (double)tv.tv_usec;
}

void print_vector(string message, vector<int> vec)
{
    cout << endl
         << message;
    cout << "[";
    for (auto it : vec)
    {
        cout << " " << it;
    }
    cout << "]" << endl;
}

int main(int argc, char **argv)
{

    if (argc != 6)
    {
        usage();
        return 0;
    }

    // blk - block size - 存储环境中的块大小
    // pkt - packet size - 内存环境中,一次计算的大小
    // slice - 对packet进行细分之后的subpkt

    // ex ./build/Tester 4 2 4 1 1
    int n = atoi(argv[1]);      // 4
    int k = atoi(argv[2]);      // 2
    int w = atoi(argv[3]);      // 4 分包数量
    int pktKiB = atoi(argv[4]); // 1
    int pktbytes = pktKiB * 1024;
    int blkMiB = atoi(argv[5]); // 1
    int blkbytes = blkMiB * 1048576;
    int sub_pkt_bytes = pktbytes / w;

    // 0.0  get repairIdx
    // int subPacket_amount = atoi(argv[6]); // 所要修复的 sub_packet 数量
    cout << "input subPacket indexs spaced with space " << endl;
    vector<int> repair_subpacket_indexs;
    int i;
    while (cin >> i)
    { // input repair_subpacket_indexs
        repair_subpacket_indexs.push_back(i);
        if ('\n' == cin.get())
        {
            break;
        }
    }

    unordered_set<int> repair_indexs;
    for (auto it : repair_subpacket_indexs)
    {
        repair_indexs.insert(it / w); // 将sub_packet所在的块加入到repair_index中
    }
    assert(repair_indexs.size() == 1);

    int repairIdx = *repair_indexs.begin(); // 所要修复的chunk索引 未知
    cout << "repairIdx = " << repairIdx << endl;

    ECBase *ec = new Clay(n, k, w, {to_string(n - 1)});
    ECBase *dec = new Clay(n, k, w, {to_string(n - 1)});

    w = ec->_w;                          // w修正
    cout << "w: " << w << endl;          // 即 cube的高为4
    int stripenum = blkbytes / pktbytes; // block中有多少个packet,
    int slicebytes = pktbytes / w;       // slice

    string stripename = "stripe0";
    vector<string> blklist;
    for (int i = 0; i < n; i++)
    {
        string blkname = "blk" + to_string(i);
        blklist.push_back(blkname);
    }

    // 0.1 prepare buffers
    char **buffers = (char **)calloc(n, sizeof(char *)); // n个packet的buffer
    for (int i = 0; i < n; i++)
    {
        // 每个buffer对应 stripe中的一个packet
        buffers[i] = (char *)calloc(pktbytes, sizeof(char));
        memset(buffers[i], 0, pktbytes);

        // 对于数据块, 填充cid,对于parity块,填充0
        if (i < k)
        {
            for (int j = 0; j < w; j++) // 填装 sub_pkt
            {
                int cid = i * w + j;
                memset(buffers[i], cid, pktbytes);
            }
        }
    }
    char *repairbuffer = (char *)calloc(pktbytes, sizeof(char)); // 需要修复的pkt的buffer

    // 1. prepare for encode
    ECDAG *encdag = nullptr;
    unordered_map<int, char *> encodeBufMap; // slice_index -> buffer_pointer

    // 1.1 create encode tasks
    encdag = ec->Encode(); // 封装encdag
    cout << ">>> encode is finished" << endl;

    // // 1.1.1 split
    // cout << endl
    //      << ">>> split is begining" << endl;
    // // vector<ECDAG *> *split_ecdags = encdag->split();
    // ECDAG* split_ecdag = encdag->split_1({8,9});
    // cout << ">>> split is finished" << endl;

    // // 1.1.2 faltten
    // cout << endl
    //      << ">>> flatten is begining" << endl;
    // vector<ECDAG *> flatten_ecdags;
    // cout << split_ecdags->size() << endl;
    // for (ECDAG *dag : *split_ecdags)
    // {
    //     ECDAG *flatten = dag->flatten();
    //     flatten_ecdags.push_back(flatten);
    // }
    // cout << ">>> flatten is finished" << endl;
    // encdag->genECUnits();

    // 1.2 generate ComputeTask from ECUnits
    vector<ComputeTask *> encctasklist;
    encdag->genComputeTaskByECUnits(encctasklist); // 这里是调用了ecdag的对blk处理的task用于clay中的slice

    // 1.3 put slices into encodeBufMap
    for (int i = 0; i < n; i++)
    {
        for (int j = 0; j < w; j++)
        {
            int idx = i * w + j;
            char *buf = buffers[i] + j * slicebytes; // 指向slice
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
    unordered_map<int, char *> decodeBufMap; //

    // 2.1 create decode tasks
    vector<int> availIdx;
    vector<int> toRepairIdx; // 需要修复的idx
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
    print_vector("availIdx =", availIdx);
    print_vector("toRepairIdx = ", repair_subpacket_indexs);

    // decdag = dec->Decode(availIdx, toRepairIdx);
    decdag = dec->Decode(availIdx, toRepairIdx);

    

    // split 
    cout << endl
         << ">>> split is begining" << endl;
    ECDAG* split_decdag = decdag->split_1(repair_subpacket_indexs);
    cout << ">>> split is finished" << endl;

    // decdag->Concact(toRepairIdx);
    
    // 2.2 generate ComputeTask from ECUnits
    vector<ComputeTask *> decctasklist;
    // decdag->genECUnits();
    // decdag->genComputeTaskByECUnits(decctasklist);

    split_decdag->genECUnits();
    split_decdag->genComputeTaskByECUnits(decctasklist);
    
    

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

    // 3 test
    double encodeTime = 0, decodeTime = 0;
    srand((unsigned)1234);
    cout << "stripenum: " << stripenum << endl;
    for (int stripei = 0; stripei < stripenum; stripei++) // 对于每个stripe
    {
        // 2.1 initialize databuffers

        // clean codebuffers
        for (int i = k; i < n; i++)
        {
            memset(buffers[i], 0, pktbytes);
        }

        //// initialize databuffers with rand
        // for (int i=0; i<k; i++) {
        //     for (int j=0; j<pktbytes; j++) {
        //         buffers[i][j] = rand();
        //     }
        // }

        // initialize databuffers with c
        for (int i = 0; i < k; i++)
        {
            for (int j = 0; j < w; j++)
            {
                char c = i * w + j;
                memset(buffers[i] + j * slicebytes, c, slicebytes);
            }
        }

        // 2.2 encode test
        encodeTime -= getCurrentTime();
        for (int taskid = 0; taskid < encctasklist.size(); taskid++)
        {
            ComputeTask *cptask = encctasklist[taskid];
            vector<int> srclist = cptask->_srclist; // from_slices
            vector<int> dstlist = cptask->_dstlist; // to_slices
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

            int col = srclist.size(); // from_size
            int row = dstlist.size(); // to_size
            int *matrix = (int *)calloc(row * col, sizeof(int)); // encode_matrix
            char **data = (char **)calloc(col, sizeof(char *));  // data_buffer
            char **code = (char **)calloc(row, sizeof(char *));  // code_buffer
            // prepare data buf
            for (int bufIdx = 0; bufIdx < srclist.size(); bufIdx++) // 遍历data slices
            {
                int child = srclist[bufIdx];
                // check whether there is buf in databuf
                assert(encodeBufMap.find(child) != encodeBufMap.end());
                data[bufIdx] = encodeBufMap[child]; // 填装data_slices到data_buffer
                unsigned char c = data[bufIdx][0];
            }
            // prepare code buf and matrix
            for (int codeBufIdx = 0; codeBufIdx < dstlist.size(); codeBufIdx++)
            {
                int target = dstlist[codeBufIdx];
                char *codebuf;
                assert(encodeBufMap.find(target) != encodeBufMap.end());
                code[codeBufIdx] = encodeBufMap[target]; // 填装code_slice到code_buffer (既然code是要求的,那么这里的填装意义何在?)
                vector<int> curcoef = coefs[codeBufIdx];
                for (int j = 0; j < col; j++)
                {
                    matrix[codeBufIdx * col + j] = curcoef[j];
                }
            }
            // perform computation
            Computation::Multi(code, data, matrix, row, col, slicebytes, "Isal"); // code和data是 char** 还能够通过encode_map获取到编码之后的结果

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

        // check correcness for decode
        bool success = true;
        for (auto subpacket_index : repair_subpacket_indexs)
        {
            int offset = subpacket_index % w; // packet内偏移
            char *buffer = decodeBufMap[subpacket_index];

            for (int i = 0; i < slicebytes; i++)
            {
                if (buffer[i] != repairbuffer[offset * sub_pkt_bytes + i])
                {
                    success = false;
                    cout << "repair error at offset: " << i << endl;
                    break;
                }
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
