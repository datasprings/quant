
#include <cstdlib>
#include <vector>
#include <chrono>
#include <tuple>
#include <cmath>
#include <fstream>
#include <iostream>

#include "../lib/trader.hpp"

void Trader::init(std::vector<std::vector<unsigned int>> shape) {
    for(unsigned int l = 0; l < shape.size(); l++) {
        unsigned int in = shape[l][0], out = shape[l][1];
        agent.add_layer(in, out);
        target.add_layer(in, out);
    }

    seed.seed(std::chrono::system_clock::now().time_since_epoch().count());
    agent.initialize(seed);
    sync();
}

void Trader::sync() {
    for(unsigned int l = 0; l < agent.num_of_layers(); l++)
        for(unsigned int n = 0; n < agent.layer(l)->out_features(); n++)
            for(unsigned int i = 0; i < agent.layer(l)->in_features(); i++)
                target.layer(l)->node(n)->set_weight(i, agent.layer(l)->node(n)->weight(i));
}

bool Trader::sample_state(std::vector<double> &asset, std::vector<double> &vix, unsigned int t, unsigned int look_back, std::vector<double> &state) {
    std::vector<double> asset_price = {asset.begin() + t - look_back + 1, asset.begin() + t + 1};
    standardize(asset_price);
    state.insert(state.end(), asset_price.begin(), asset_price.end());

    return t + 1 == asset.size() - 1;
}

std::tuple<unsigned int, double> Trader::epsilon_greedy_policy(std::vector<double> &state, double EPSILON) {
    unsigned int action;
    double explore = (double)rand() / RAND_MAX;

    std::vector<double> agent_q = agent.predict(state);
    if(explore < EPSILON)
        action = rand() % agent.layer(agent.num_of_layers() - 1)->out_features();
    else
        action = std::max_element(agent_q.begin(), agent_q.end()) - agent_q.begin();

    std::tuple<unsigned int, double> action_q = std::make_tuple(action, agent_q[action]);
    return action_q;
}

void Trader::optimize(std::vector<double> &asset, std::vector<double> &vix, double EPSILON_INIT, double EPSILON_MIN, double ALPHA_INIT, double ALPHA_MIN,
                      double GAMMA, unsigned int MEMORY_CAPACITY, unsigned int BATCH_SIZE, unsigned int SYNC_INTERVAL, unsigned int LOOK_BACK, std::string checkpoint) {
    double EPSILON = EPSILON_INIT;
    double ALPHA = ALPHA_INIT;

    std::vector<std::vector<double>> state_memory;
    std::vector<unsigned int> action_memory;
    std::vector<double> reward_memory;
    std::vector<std::vector<double>> next_state_memory;

    unsigned int training_count = 0;
    double loss_sum = 0.00, mean_loss = 0.00;
    double buy_and_hold = 1.00, model_return = 1.00;

    for(unsigned int t = LOOK_BACK - 1; t <= asset.size() - 2; t++) {
        std::vector<double> state;
        bool terminal = sample_state(asset, vix, t, LOOK_BACK, state);

        std::tuple<unsigned int, double> action_q = epsilon_greedy_policy(state, EPSILON);
        unsigned int action = std::get<0>(action_q);
        double q_value = std::get<1>(action_q);

        double diff = (asset[t+1] - asset[t]) / asset[t];
        double observed_reward;
        if(action == LONG)
            observed_reward = diff;
        else if(action == SHORT)
            observed_reward = -1.00 * diff;
        else
            observed_reward = 0.00;

        double expected_reward = observed_reward;
        if(!terminal) {
            std::vector<double> next_state;
            sample_state(asset, vix, t+1, LOOK_BACK, next_state);

            std::vector<double> target_q = target.predict(next_state);
            expected_reward += GAMMA * *std::max_element(target_q.begin(), target_q.end());

            next_state_memory.push_back(next_state);
        }

//        std::cout << "@frame" << t << ": ";
//        std::cout << "(diff = " << diff << ") (action = " << action << ") (observed = " << observed_reward << ") (expected = " << expected_reward << ")\n";

        state_memory.push_back(state);
        action_memory.push_back(action);
        reward_memory.push_back(expected_reward);

        if(training_count > 0) {
            buy_and_hold *= 1.00 + diff;
            model_return *= 1.00 + observed_reward;
            loss_sum += pow(expected_reward - q_value, 2);
            mean_loss = loss_sum / training_count;

            std::ofstream out("./data/log", std::ios::app);
            out << mean_loss << " " << buy_and_hold << " " << model_return << "\n";
            out.close();
        }

        if(state_memory.size() == MEMORY_CAPACITY) {
            std::vector<unsigned int> index(MEMORY_CAPACITY, 0);
            std::iota(index.begin(), index.end(), 0);
            std::shuffle(index.begin(), index.end(), seed);
            index.erase(index.begin() + BATCH_SIZE, index.end());

            for(unsigned int k: index) {
                std::vector<double> agent_q = agent.predict(state_memory[k]);
                for(int l = agent.num_of_layers() - 1; l >= 0; l--) {
                    unsigned int start = 0, end = agent.layer(l)->out_features();
                    if(l == agent.num_of_layers() - 1) {
                        start = action_memory[k];
                        end = start + 1;
                    }

                    double partial_gradient = 0.00, gradient = 0.00;
                    for(unsigned int n = start; n < end; n++) {
                        if(l == agent.num_of_layers() - 1)
                            partial_gradient = -2.00 * (reward_memory[k] - agent_q[n]);
                        else
                            partial_gradient = agent.layer(l)->node(n)->err() * relu_prime(agent.layer(l)->node(n)->sum());

                        double updated_bias = agent.layer(l)->node(n)->bias() - ALPHA * partial_gradient;
                        agent.layer(l)->node(n)->set_bias(updated_bias);

                        for(unsigned int i = 0; i < agent.layer(l)->in_features(); i++) {
                            if(l == 0)
                                gradient = partial_gradient * state_memory[k][i];
                            else {
                                gradient = partial_gradient * agent.layer(l-1)->node(i)->act();
                                agent.layer(l-1)->node(i)->add_err(partial_gradient * agent.layer(l)->node(n)->weight(i));
                            }

                            double updated_weight = agent.layer(l)->node(n)->weight(i) - ALPHA * gradient;
                            agent.layer(l)->node(n)->set_weight(i, updated_weight);
                        }
                    }
                }
            }

            state_memory.erase(state_memory.begin(), state_memory.begin() + 1);
            action_memory.erase(action_memory.begin(), action_memory.begin() + 1);
            reward_memory.erase(reward_memory.begin(), reward_memory.begin() + 1);
            next_state_memory.erase(next_state_memory.begin(), next_state_memory.begin() + 1);

            training_count++;

            EPSILON = (EPSILON_MIN - EPSILON_INIT) / (asset.size() - LOOK_BACK - 2 - MEMORY_CAPACITY) * training_count + EPSILON_INIT;
            ALPHA = (ALPHA_MIN - ALPHA_INIT) / (asset.size() - LOOK_BACK - 2 - MEMORY_CAPACITY) * training_count + ALPHA_INIT;
        }

        if(t % SYNC_INTERVAL == 0) {
            sync();
            save(checkpoint);
        }
    }

    save(checkpoint);
}

void Trader::save(std::string path) {
    agent.save(path);
}

