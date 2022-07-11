
#include <cstdlib>
#include <iostream>
#include <vector>
#include <string>

#include "../lib/trader.hpp"

int main(int argc, char *argv[])
{
    std::vector<double> series = read_csv("./data/SPXL_5min.csv", "Close");

    Trader trader;
    trader.optimize(series, "./models/checkpoint");

    return 0;
}
