#pragma once
// Mechanical transcription of the design-reviewed frozen CASE_MATRIX.json.
// SHA256 3f51d3a4c5031e0b016d328497b1bae47b4d06082862b7229557fd3c6f4a8259
#include <map>
#include <string>
#include <vector>
namespace mdds::experiment::matrix {
struct EndpointSpec {std::string id;bool board_a;unsigned participant;bool writer;unsigned entity_id,domain;std::string topic;bool reliable,transient_local,late;};
using WriteSet=std::map<std::string,std::vector<unsigned>>;
using ReaderSets=std::map<std::string,WriteSet>;
struct CaseSpec {std::string id;std::vector<EndpointSpec> endpoints;WriteSet pre,post;std::vector<std::vector<std::string>> initial_edges,final_edges,forbidden_edges;ReaderSets final_samples;std::vector<std::string> deletions;ReaderSets historical;};
inline const std::vector<CaseSpec>& Cases(){static const std::vector<CaseSpec> value={
{"M1",{{"A1.W1",true,1,true,1,73,"T",true,false,false},{"B1.R1",false,1,false,11,73,"T",true,false,false},{"B1.R2",false,1,false,12,73,"T",true,false,false}},{{"A1.W1",{1,2,3,4,5,6,7,8,9,10}}},{},{{"A1.W1","B1.R1"},{"A1.W1","B1.R2"}},{{"A1.W1","B1.R1"},{"A1.W1","B1.R2"}},{},{{"B1.R1",{{"A1.W1",{1,2,3,4,5,6,7,8,9,10}}}},{"B1.R2",{{"A1.W1",{1,2,3,4,5,6,7,8,9,10}}}}},{},{}},
{"M2",{{"A1.W1",true,1,true,1,73,"T",true,false,false},{"B1.R1",false,1,false,11,73,"T",true,false,false},{"B2.R1",false,2,false,11,73,"T",true,false,false}},{{"A1.W1",{1,2,3,4,5,6,7,8,9,10}}},{},{{"A1.W1","B1.R1"},{"A1.W1","B2.R1"}},{{"A1.W1","B1.R1"},{"A1.W1","B2.R1"}},{},{{"B1.R1",{{"A1.W1",{1,2,3,4,5,6,7,8,9,10}}}},{"B2.R1",{{"A1.W1",{1,2,3,4,5,6,7,8,9,10}}}}},{},{}},
{"M3",{{"A1.W1",true,1,true,1,73,"T",true,false,false},{"A1.W2",true,1,true,2,73,"T",true,false,false},{"B1.R1",false,1,false,11,73,"T",true,false,false}},{{"A1.W1",{1,2,3,4,5,6,7,8,9,10}},{"A1.W2",{1,2,3,4,5,6,7,8,9,10}}},{},{{"A1.W1","B1.R1"},{"A1.W2","B1.R1"}},{{"A1.W1","B1.R1"},{"A1.W2","B1.R1"}},{},{{"B1.R1",{{"A1.W1",{1,2,3,4,5,6,7,8,9,10}},{"A1.W2",{1,2,3,4,5,6,7,8,9,10}}}}},{},{}},
{"M4",{{"A1.W1",true,1,true,1,73,"T",true,false,false},{"B1.R1",false,1,false,11,73,"T",true,false,false},{"B1.R2",false,1,false,12,73,"T",true,false,false}},{{"A1.W1",{1,2,3,4,5,6,7,8,9,10}}},{{"A1.W1",{11,12,13,14,15,16,17,18,19,20}}},{{"A1.W1","B1.R1"},{"A1.W1","B1.R2"}},{{"A1.W1","B1.R2"}},{},{{"B1.R1",{{"A1.W1",{1,2,3,4,5,6,7,8,9,10}}}},{"B1.R2",{{"A1.W1",{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20}}}}},{"B1.R1"},{}},
{"Q_BE_BE",{{"A1.W1",true,1,true,1,73,"T",false,false,false},{"B1.R1",false,1,false,11,73,"T",false,false,false}},{{"A1.W1",{1,2,3}}},{},{{"A1.W1","B1.R1"}},{{"A1.W1","B1.R1"}},{},{{"B1.R1",{{"A1.W1",{1,2,3}}}}},{},{}},
{"Q_REL_BE",{{"A1.W1",true,1,true,1,73,"T",true,false,false},{"B1.R1",false,1,false,11,73,"T",false,false,false}},{{"A1.W1",{1,2,3}}},{},{{"A1.W1","B1.R1"}},{{"A1.W1","B1.R1"}},{},{{"B1.R1",{{"A1.W1",{1,2,3}}}}},{},{}},
{"Q_REL_REL",{{"A1.W1",true,1,true,1,73,"T",true,false,false},{"B1.R1",false,1,false,11,73,"T",true,false,false}},{{"A1.W1",{1,2,3}}},{},{{"A1.W1","B1.R1"}},{{"A1.W1","B1.R1"}},{},{{"B1.R1",{{"A1.W1",{1,2,3}}}}},{},{}},
{"Q_BE_REL",{{"A1.W1",true,1,true,1,73,"T",false,false,false},{"B1.R1",false,1,false,11,73,"T",true,false,false},{"A1.WC",true,1,true,3,73,"CONTROL",true,false,false},{"B1.RC",false,1,false,13,73,"CONTROL",true,false,false}},{{"A1.W1",{1,2,3}},{"A1.WC",{1,2,3}}},{},{{"A1.WC","B1.RC"}},{{"A1.WC","B1.RC"}},{{"A1.W1","B1.R1"}},{{"B1.R1",{}},{"B1.RC",{{"A1.WC",{1,2,3}}}}},{},{}},
{"L1",{{"A1.W1",true,1,true,1,73,"T",true,false,false},{"B1.R1",false,1,false,11,73,"T",true,false,true}},{{"A1.W1",{1,2,3}}},{{"A1.W1",{4,5,6}}},{},{{"A1.W1","B1.R1"}},{},{{"B1.R1",{{"A1.W1",{4,5,6}}}}},{},{}},
{"L2",{{"A1.W1",true,1,true,1,73,"T",true,true,false},{"B1.R1",false,1,false,11,73,"T",true,true,true}},{{"A1.W1",{1,2,3}}},{{"A1.W1",{4,5,6}}},{},{{"A1.W1","B1.R1"}},{},{{"B1.R1",{{"A1.W1",{1,2,3,4,5,6}}}}},{},{{"B1.R1",{{"A1.W1",{1,2,3}}}}}},
{"L3",{{"A1.W1",true,1,true,1,73,"T",true,true,false},{"B1.R1",false,1,false,11,73,"T",true,true,false},{"B1.R2",false,1,false,12,73,"T",true,true,true}},{{"A1.W1",{1,2,3}}},{{"A1.W1",{4,5,6}}},{{"A1.W1","B1.R1"}},{{"A1.W1","B1.R1"},{"A1.W1","B1.R2"}},{},{{"B1.R1",{{"A1.W1",{1,2,3,4,5,6}}}},{"B1.R2",{{"A1.W1",{1,2,3,4,5,6}}}}},{},{{"B1.R2",{{"A1.W1",{1,2,3}}}}}},
{"I1",{{"A1.W1",true,1,true,1,73,"T",true,false,false},{"B1.R1",false,1,false,11,73,"T",true,false,false},{"B1.R2",false,1,false,12,73,"OTHER",true,false,false}},{{"A1.W1",{1,2,3}}},{},{{"A1.W1","B1.R1"}},{{"A1.W1","B1.R1"}},{{"A1.W1","B1.R2"}},{{"B1.R1",{{"A1.W1",{1,2,3}}}},{"B1.R2",{}}},{},{}},
{"I2",{{"A1.W1",true,1,true,1,73,"T",true,false,false},{"B1.R1",false,1,false,11,73,"T",true,false,false},{"A2.W1",true,2,true,1,74,"T",true,false,false},{"B2.R1",false,2,false,11,74,"T",true,false,false}},{{"A1.W1",{1,2,3}},{"A2.W1",{1,2,3}}},{},{{"A1.W1","B1.R1"},{"A2.W1","B2.R1"}},{{"A1.W1","B1.R1"},{"A2.W1","B2.R1"}},{{"A1.W1","B2.R1"},{"A2.W1","B1.R1"}},{{"B1.R1",{{"A1.W1",{1,2,3}}}},{"B2.R1",{{"A2.W1",{1,2,3}}}}},{},{}},
};return value;}
}
