#ifndef _AEQUITAS_H
#define _AEQUITAS_H


#include <vector>
#include <map>
#include <queue>
#include <cstdlib>
#include <iostream>
#include <cstring>
#include "hotstuff/type.h"
#include "hotstuff/entity.h"


//TODO: deal with cmds appear less than (n - 2f) times
//TODO: deal with "static cmp"
namespace  Aequitas {
const int max_number_cmds = 100;
class TopologyGraph
{
    private:
    //Arrays for adding edges & graph
    std::vector<int> edge[max_number_cmds];
    int cnt, top;
    int bel[max_number_cmds], dfn[max_number_cmds], low[max_number_cmds], stck[max_number_cmds];
    bool inst[max_number_cmds];

    public:
    int scc;
    std::vector<int> scc_have[max_number_cmds];
    //Arrays for graph after scc
    std::vector<int> edge_with_scc[max_number_cmds];
    int inDegree[max_number_cmds];

    private:

    int min(int i, int j)
    {
        if (i < j) return i;
        return j;
    }

    //find strong connected component
    void tarjan(int u)
    {
        dfn[u] = low[u] = ++cnt;
        stck[++top] = u;
        inst[u] = true;
        for (int l = 0; l < edge[u].size(); ++l)
        {
            int v = edge[u][l];
            if (!dfn[v]){
                tarjan(v);
                low[u] = min(low[u], low[v]);
            } else if (inst[v]) low[u] = min(low[u], dfn[v]);
        }
        if (dfn[u] == low[u])
        {
            ++scc;
            int v;
            do{
                v = stck[top--];
                bel[v] = scc;
                scc_have[scc].push_back(v);
                inst[v] = false;
            } while (v != u);
        }
    }

    void clear_array()
    {
        for(int i = 0; i < max_number_cmds; i++)
        {
            edge[i].clear();
            scc_have[i].clear();
            edge_with_scc[i].clear();
        }
        cnt = 0, top = 0, scc = 0;
        std::memset(bel, 0, sizeof(bel));
        std::memset(dfn, 0, sizeof(dfn));
        std::memset(low, 0, sizeof(low));
        std::memset(stck, 0, sizeof(stck));
        std::memset(inst, 0, sizeof(inst));
        std::memset(inDegree, 0, sizeof(inDegree));
    }    

    public:

    TopologySort() { clear_array(); }
    ~TopologySort() {}

    void addedge(int i, int j)
    {
        edge[i].push_back(j);
    }

    void find_scc(int distinct_cmd)
    {
        for (int i = 1; i <= distinct_cmd; i++)
            if (!dfn[i])
                tarjan(i);
    }

    void topology_sort(int distinct_cmd)
    {
        for (int i = 1; i <= distinct_cmd; i++)
        {
            for (int j = 0; j < edge[i].size(); j++)
            {
                int ii = bel[i], jj = bel[j];
                if(ii != jj)
                {
                    edge_with_scc[ii].push_back(jj);
                    ++inDegree[jj];
                }
            }
        }
    }
    
     
};

//decide whether we should add an edge from cmd_j to cmd_i
//if in more than threshold_number replicas, cmd_j is before cmd_i, then we'll add an edge
//you can add the granularity "g" here if needed
bool run_before(int j, int i, std::vector<hotstuff::OrderedList> &proposed_orderlist, int threshold_number)
{
    int n_replica = proposed_orderlist.size();
    int n_cmds = proposed_orderlist[0].cmds.size();
    int count = 0;
    for(int ii = 0; ii < n_replica; ii++)
    {
        for(int jj = 0; jj < proposed_orderlist[ii].cmds.size(); jj++)
        {
            if(proposed_orderlist[ii].cmds[jj] == proposed_orderlist[0].cmds[j]) {count++; break;}
            if(proposed_orderlist[ii].cmds[jj] == proposed_orderlist[0].cmds[i]) break;
        }
    }
    if(count > threshold_number) return true;
    return false;
}

// g should be a float between (0,1), if cmd1 appears before cmd2 in g*number_of_nodes, one edge from cmd1 to cmd2 will be added
//proposed_orderlist[0] is the orderlist of the leader before the leader receive other replicas' ordered list
//return a vector, which will be a list of orderedlist
//"timestamps" in these returned orderedlist are useless, cmds in one orderedlist should be in one block
std::vector<hotstuff::OrderedList> aequitas_order(std::vector<hotstuff::OrderedList> &proposed_orderlist, double g)
{
    int n_replica = proposed_orderlist.size();
    if(n_replica == 0) 
        throw std::runtime_error("the number of replica is 0.");
    int n_cmds = proposed_orderlist[0].cmds.size();
    if(n_cmds == 0 || (n_cmds != proposed_orderlist[0].timestamps.size()))
        throw std::runtime_error("no cmds to be ordered or cmds not in the right form.");
    
    //sort all the cmds TODO
    //for (int i = 0; i < n_replica; i++) proposed_orderlist[i].sort_cmds();

    //map the cmd to a number
    int distinct_cmd = 0;
    std::vector<uint256_t> cmd_content; cmd_content.clear(); cmd_content.push_back(0);
    std::map<uint256_t, int> map_cmd; map_cmd.clear();

    TopologyGraph G;

    for (int i = 0; i < n_cmds; i++)
    {
        uint256_t cmd_i = proposed_orderlist[0].cmds[i];
        if (!map_cmd[cmd_i])
            map_cmd[cmd_i] = ++distinct_cmd, cmd_content.push_back(cmd_i);
        int ii = map_cmd[cmd_i];
        for (int j = 0; j < n_cmds; j++)
        {
            uint256_t cmd_j = proposed_orderlist[0].cmds[j];
            if (!map_cmd[cmd_j]) map_cmd[cmd_j] = ++distinct_cmd, cmd_content.push_back(cmd_j);
            int jj = map_cmd[cmd_j];
            if (j != i && run_before(j, i, proposed_orderlist, g * n_replica))
            {

                //add edge from jj to ii
                G.addedge(jj, ii);
            }
        }
    }

    //find scc
    G.find_scc(distinct_cmd);
    
    //topology sort start...
    G.topology_sort(distinct_cmd);
    
    //now deal with graph after scc
    std::queue<int> que = std::queue<int>();
    int check_whether_all_cmds_are_ordered = 0;
    std::vector<hotstuff::OrderedList> final_ordered_vector; final_ordered_vector.clear();
    for (int i = 1; i <= G.scc; i++)
    {
        if (G.inDegree[i] == 0) que.push(i);
    }
    while(!que.empty())
    {
        std::vector<uint256_t> cmds; cmds.clear();
        std::vector<uint64_t> timestamps; timestamps.clear();
        std::vector<int> to_be_added; to_be_added.clear();
        while(!que.empty())
        {
            int u = que.front();
            que.pop();
            for (int i = 0; i < G.scc_have[u].size(); i++)
            {
                cmds.push_back(cmd_content[G.scc_have[u][i]]);
                check_whether_all_cmds_are_ordered++;
            }
            for (int i = 0; i < G.edge_with_scc[u].size(); i++)
            {
                int v = G.edge_with_scc[u][i];
                --G.inDegree[v];
                if(G.inDegree[v] == 0)
                    to_be_added.push_back(v);
            }
            G.edge_with_scc[u].clear();
        }
        hotstuff::OrderedList final_ordered_list(cmds, timestamps);
        final_ordered_vector.push_back(final_ordered_list);

        for(int i = 0; i < to_be_added.size(); i++)
            que.push(to_be_added[i]);
    }
    if (check_whether_all_cmds_are_ordered != distinct_cmd)
        throw std::runtime_error("Aequitas failed to topology sort the commands...");
    return final_ordered_vector;
}

}


#endif
