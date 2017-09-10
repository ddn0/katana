#include "Galois/Bag.h"
#include "Galois/Runtime/Serialize.h"
#include <boost/range/iterator_range.hpp>
#include <iostream>
#include <algorithm>
#include <sstream>

//TODO: move to libdist or delete
namespace Galois {
namespace WorkList {


template <typename WL, typename GraphTy>
class WLdistributed: public WL {

  //typedef typename WL::value_type workItemTy;
  typedef typename WL::value_type value_type;
  std::vector<Galois::InsertBag<uint64_t>> bag_vec;
  //std::vector<std::vector<uint64_t>> bag_vec;
  WL workList;
  std::deque<uint64_t> local_wl;
  GraphTy& graph;
  bool canTerminate;

public:

  WLdistributed (GraphTy& _graph): workList (),graph(_graph) {
    auto& net = Galois::Runtime::getSystemNetworkInterface();
    bag_vec.resize(net.Num);
  }

  void push (const typename WL::value_type& v) {
    workList.push(v);
  }

  template <typename I>
  void push (I b, I e) {
    workList.push(b,e);
  }

  void convertGID2LID_localWL(){
    uint32_t num = size();
    Galois::do_all(boost::counting_iterator<uint32_t>(0),
        boost::counting_iterator<uint32_t>(num),
        [&](uint32_t n) {
          local_wl[n] = graph.G2L(local_wl[n]);
        },
        Galois::loopname("G2L_wl"),
        Galois::numrun(graph.get_run_identifier()));
  }

  void insertToLocalWL(Galois::InsertBag<uint64_t>& bag){
    auto headerVec = bag.getHeads();
    size_t totalSize = bag.size();
    for(auto head : headerVec){
      local_wl.insert(local_wl.end(), head->dbegin, head->dend);
    }
    assert(local_wl.size() == totalSize);
    bag.clear();
  }

  void insertToLocalWL(std::deque<uint64_t>& d){
    local_wl.insert(local_wl.end(), d.begin(), d.end());
  }

  void sync(){
    auto& net = Galois::Runtime::getSystemNetworkInterface();
    std::string statSendBytes_str("WL_SEND_BYTES_" + graph.get_run_identifier());
    std::string timer_wl_send_recv_str("WL_SYNC_TIME_SEND_RECV_" + graph.get_run_identifier());
    std::string timer_wl_total_str("WL_SYNC_TIME_TOTAL_" + graph.get_run_identifier());

    Galois::StatTimer StatTimer_total_sync(timer_wl_total_str.c_str());
    Galois::StatTimer StatTimer_send_recv(timer_wl_send_recv_str.c_str());
    Galois::Statistic send_bytes(statSendBytes_str);

    StatTimer_total_sync.start();
    //Did work in the previous round
    if(local_wl.size() > 0 )
      canTerminate = false;
    else
      canTerminate = true;

    clear_wl();

    Galois::on_each([&] (const unsigned tid, const unsigned numT){
        Galois::optional<value_type> p;
        while(p = workList.pop()){
        auto GID = graph.getGID(*p);
        assert(graph.getHostID(GID) < bag_vec.size());
        bag_vec[graph.getHostID(GID)].push(GID);
        }
        }, Galois::loopname("worklist bags"));

    StatTimer_send_recv.start();
    for(auto h = 0; h < net.Num; ++h){
      if(canTerminate && (bag_vec[h].size() > 0)){
        canTerminate = false;
      }
      if(h == net.ID){
        insertToLocalWL(bag_vec[h]);
        continue;
      }

      size_t bagSize =  bag_vec[h].size();
      Galois::Runtime::SendBuffer b;
      Galois::Runtime::gSerialize(b, canTerminate);
      Galois::Runtime::gSerialize(b, bag_vec[h]);
      send_bytes += b.size();
      //assert(b.size() == sizeof(uint64_t)*(bagSize + 2));
      //std::stringstream ss;
      //ss << net.ID << " ] : SENDING : " << bagSize << "\n";
      //std::cerr << ss.str();
      net.sendTagged(h, Galois::Runtime::evilPhase, b);
    }
    net.flush();

    //receive
    for (auto h = 0; h < net.Num; ++h) {
      if ((h == net.ID))
        continue;
      decltype(net.recieveTagged(Galois::Runtime::evilPhase,nullptr)) p;
      do {
        net.handleReceives();
        p = net.recieveTagged(Galois::Runtime::evilPhase, nullptr);
      } while (!p);
      bool other_canTerminate = true;
      Galois::Runtime::gDeserialize(p->second,other_canTerminate);
      canTerminate = (other_canTerminate && canTerminate);

      std::deque<uint64_t> s;
      //TODO avoid double copy
      Galois::Runtime::gDeserialize(p->second,s);
      insertToLocalWL(s);
      //std::stringstream ss;
      //ss << net.ID << " ] : RECV : " << s.size() << " other CT : " << other_canTerminate << "\n";
      //std::cerr << ss.str();
    }
    ++Galois::Runtime::evilPhase;
    StatTimer_send_recv.stop();

    //Sort and remove duplicates from the worklist.
    std::sort(local_wl.begin(), local_wl.end());
    std::unique(local_wl.begin(), local_wl.end());
    //TODO Check that duplicates are being removed
    //assert(local_wl.size() <= graph.numOwned)

    //Convert Global to Local IDs
    convertGID2LID_localWL();
    StatTimer_total_sync.stop();

  }

  bool can_terminate(){
    return canTerminate;
  }

  template<typename Ty>
    void push_initial(Ty e) {
      local_wl.push_back(e);
    }

  template<typename Itr>
    void push_initial(Itr b, Itr e) {
      local_wl.assign(b,e);
    }

  Galois::optional<value_type> pop() {
    return local_wl.pop_back();
  }

  auto getRange() {
    return boost::make_iterator_range(local_wl.begin(), local_wl.end());
  }

  auto clear_wl() {
    local_wl.clear();
  }

  bool empty() const {
    return local_wl.empty();
  }

  size_t size() const {
    return local_wl.size();
  }

};


} // end namespace WorkList
} // end namespace Galois

