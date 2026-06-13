#pragma once
#include <vector>
#include <queue>
#include "globals.h"
#include "lua_manager.h"

// Grid A* — returns the next tile to step onto toward (tx,ty) from (sx,sy).
// Returns {sx,sy} if already at target or no path found.
// Search is bounded to the NPC-target bounding box + MARGIN,
// keeping the local grid small (worst case ~(AGRO_DETECT_RANGE+MARGIN)^2).
inline std::pair<short,short> astar_next_step(short sx, short sy, short tx, short ty)
{
    if (sx == tx && sy == ty) return { sx, sy };

    constexpr int MARGIN = 4;

    int x0 = std::max(0,             std::min((int)sx,(int)tx) - MARGIN);
    int y0 = std::max(0,             std::min((int)sy,(int)ty) - MARGIN);
    int x1 = std::min(WORLD_WIDTH-1, std::max((int)sx,(int)tx) + MARGIN);
    int y1 = std::min(WORLD_HEIGHT-1,std::max((int)sy,(int)ty) + MARGIN);

    int W = x1 - x0 + 1;
    int H = y1 - y0 + 1;
    int N = W * H;

    int lsx = sx - x0, lsy = sy - y0;
    int ltx = tx - x0, lty = ty - y0;

    if (lsx < 0 || lsx >= W || lsy < 0 || lsy >= H) return { sx, sy };
    if (ltx < 0 || ltx >= W || lty < 0 || lty >= H) return { sx, sy };

    std::vector<float> g_cost(N, 1e9f);
    std::vector<int>   parent(N, -1);
    std::vector<bool>  closed(N, false);

    auto idx = [&](int x, int y) { return y * W + x; };

    struct Node {
        float f; int x, y;
        bool operator>(const Node& o) const { return f > o.f; }
    };
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> pq;

    int si = idx(lsx, lsy);
    g_cost[si] = 0.f;
    pq.push({ (float)(std::abs(lsx-ltx) + std::abs(lsy-lty)), lsx, lsy });

    constexpr int DX[4] = {  0, 0, -1, 1 };
    constexpr int DY[4] = { -1, 1,  0, 0 };

    bool found = false;
    while (!pq.empty()) {
        auto [f, cx, cy] = pq.top(); pq.pop();
        int ci = idx(cx, cy);
        if (closed[ci]) continue;
        closed[ci] = true;

        if (cx == ltx && cy == lty) { found = true; break; }

        for (int d = 0; d < 4; ++d) {
            int nx = cx + DX[d], ny = cy + DY[d];
            if (nx < 0 || nx >= W || ny < 0 || ny >= H) continue;
            if (is_obstacle(nx + x0, ny + y0)) continue;
            int ni = idx(nx, ny);
            if (closed[ni]) continue;
            float ng = g_cost[ci] + 1.f;
            if (ng < g_cost[ni]) {
                g_cost[ni] = ng;
                parent[ni]  = ci;
                float h = (float)(std::abs(nx-ltx) + std::abs(ny-lty));
                pq.push({ ng + h, nx, ny });
            }
        }
    }

    if (!found) return { sx, sy };

    // 목표에서 역추적해 출발지 바로 다음 노드(= 첫 스텝) 찾기
    int cur = idx(ltx, lty);
    while (parent[cur] != si && parent[cur] != -1)
        cur = parent[cur];

    if (parent[cur] == -1) return { sx, sy };

    return { (short)(cur % W + x0), (short)(cur / W + y0) };
}
