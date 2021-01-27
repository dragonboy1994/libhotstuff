/*
* For Test
*/

#include <vector>
#include <map>
#include <queue>
#include <cstdlib>
#include <algorithm>
#include <iostream>
#include "hotstuff/type.h"
#include "hotstuff/entity.h"
#include "hotstuff/aequitas.h"



int main()
{
    double g = 3.0/4.0;
    int n = 5;
    int n_cmds = 4;
    std::vector<int> timestamps__;
    std::vector<OrderedList> proposed_orderlist;
    for (int i = 1; i <= n_cmds; i++) timestamps__.push_back(i);
    for (int i = 1; i <= n; i++)
    {
        std::vector<uint64_t> timestamps;
        std::vector<uint256_t> cmds;
        for(int i = 0; i < n_cmds; i++)
        {
            timestamps.push_back((uint64_t)(timestamps__[i]));
            cmds.push_back((uint256_t)(timestamps__[i]));
        }
        OrderedList cur_proposed_orderlist(timestamps, cmds);
        proposed_orderlist.push_back(cur_proposed_orderlist);
        std::next_permutation(timestamps__.begin(), timestamps__.end());
    }
    std::vector<OrderedList> final_orderlist = Aequitas::aequitas_order(proposed_orderlist, g);
    for (int i = 0; i < final_orderlist.size(); i++)
    {
        final_orderlist[i].printout();
        std::cout << "===== next block ===\n";
    }
    return 0;
}








