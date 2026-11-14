#include "history.hpp"
int main(int argc,char** argv){try{d4::check(argc==2,"usage: d4_history_gateway CONFIG");d4::HistoryGateway gateway(d4::config(argv[1]));return gateway.run();}catch(const std::exception& e){std::cerr<<"D4_CONFIG_FAIL "<<e.what()<<std::endl;return 2;}}
