#ifndef VERTEX_MINER_H
#define VERTEX_MINER_H
#include "miner.h"
typedef std::unordered_map<BaseEmbedding, Frequency> SimpleMap;
typedef QuickPattern<EdgeInducedEmbedding<StructuralElement>, StructuralElement> StrQPattern; // structural quick pattern
typedef CanonicalGraph<EdgeInducedEmbedding<StructuralElement>, StructuralElement> StrCPattern; // structural canonical pattern
typedef std::unordered_map<StrQPattern, Frequency> StrQpMapFreq; // mapping structural quick pattern to its frequency
typedef std::unordered_map<StrCPattern, Frequency> StrCgMapFreq; // mapping structural canonical pattern to its frequency
typedef galois::substrate::PerThreadStorage<StrQpMapFreq> LocalStrQpMapFreq;
typedef galois::substrate::PerThreadStorage<StrCgMapFreq> LocalStrCgMapFreq;
typedef galois::gstl::Vector<BaseEmbedding> BaseEmbeddingBuffer;
typedef galois::gstl::Vector<VertexEmbedding> VertexEmbeddingBuffer;

class VertexMiner : public Miner {
public:
	VertexMiner(Graph *g, unsigned size = 3, int np = 1) {
		graph = g;
		max_size = size;
		degree_counting();
		npatterns = np;
		if (npatterns == 1)
			total_num.reset();
		else {
			accumulators.resize(npatterns);
			for (int i = 0; i < npatterns; i++) accumulators[i].reset();
			std::cout << max_size << "-motif has " << npatterns << " patterns in total\n";
			#ifdef USE_MAP
			for (auto i = 0; i < numThreads; i++) qp_localmaps.getLocal(i)->clear();
			#endif
		}
	}
	virtual ~VertexMiner() {}

	// Pangolin APIs
	// toExtend
	virtual bool toExtend(unsigned n, const BaseEmbedding &emb, unsigned pos) {
		return true;
	}
	// toAdd (only add non-automorphisms)
	virtual bool toAdd(unsigned n, const BaseEmbedding &emb, VertexId dst, unsigned pos) {
		VertexId src = emb.get_vertex(pos);
		return !is_vertexInduced_automorphism<BaseEmbedding>(n, emb, pos, src, dst);
	}
	virtual bool toExtend(unsigned n, const VertexEmbedding &emb, unsigned pos) {
		return true;
	}
	// toAdd (only add non-automorphisms)
	virtual bool toAdd(unsigned n, const VertexEmbedding &emb, VertexId dst, unsigned pos) {
		VertexId src = emb.get_vertex(pos);
		return !is_vertexInduced_automorphism<VertexEmbedding>(n, emb, pos, src, dst);
	}
	virtual unsigned getPattern(unsigned n, unsigned i, VertexId dst, const VertexEmbedding &emb, unsigned pos) {
		return 0;
	}
	virtual void print_output() {
	}

	// Given an embedding, extend it with one more vertex. Used for cliques. (fast)
	inline void extend_vertex(BaseEmbeddingQueue &in_queue, BaseEmbeddingQueue &out_queue) {
		//galois::runtime::profileVtune([&] () {
		galois::do_all(galois::iterate(in_queue),
			[&](const BaseEmbedding& emb) {
				unsigned n = emb.size();
				for (unsigned i = 0; i < n; ++i) {
					if(!toExtend(n, emb, i)) continue;
					VertexId src = emb.get_vertex(i);
					for (auto e : graph->edges(src)) {
						GNode dst = graph->getEdgeDst(e);
						if (toAdd(n, emb, dst, i)) {
							if (n < max_size-1) { // generate a new embedding and add it to the next queue
								BaseEmbedding new_emb(emb);
								new_emb.push_back(dst);
								out_queue.push_back(new_emb);
							} else total_num += 1; // if size = max_size, no need to add to the queue, just accumulate
						}
					}
				}
			},
			galois::chunk_size<CHUNK_SIZE>(), galois::steal(), galois::no_conflicts(),
			galois::loopname("Extending")
		);
		//}, "ExtendingVtune");
	}
	/*
	inline void extend_vertex_lazy(BaseEmbeddingQueue &in_queue, BaseEmbeddingQueue &out_queue) {
		std::cout << "lazy extension\n";
		galois::do_all(galois::iterate(in_queue),
			[&](const BaseEmbedding& emb) {
				unsigned n = emb.size();
				VertexId src = emb.get_vertex(n-1);
				UintList buffer; 
				for (auto e : graph->edges(src)) {
					GNode dst = graph->getEdgeDst(e);
					buffer.push_back(dst);
				}
				for (auto dst : buffer) {
					if (toAdd(n, emb, dst, n-1)) {
						if (n < max_size-1) {
							BaseEmbedding new_emb(emb);
							new_emb.push_back(dst);
							out_queue.push_back(new_emb);
						} else total_num += 1;
					}
				}
			},
			galois::chunk_size<CHUNK_SIZE>(), galois::steal(), galois::no_conflicts(),
			galois::loopname("Extending")
		);
	}
	//*/
	// Given an embedding, extend it with one more vertex. Used for vertex-induced motif
	inline void extend_vertex(VertexEmbeddingQueue &in_queue, VertexEmbeddingQueue &out_queue) {
		//galois::runtime::profilePapi([&] () {
		// for each embedding in the task queue, do vertex-extension
		galois::do_all(galois::iterate(in_queue),
			[&](const VertexEmbedding& emb) {
				unsigned n = emb.size();
				#ifdef USE_MAP
				StrQpMapFreq *qp_lmap;
				if (n >= 4) qp_lmap = qp_localmaps.getLocal();
				#endif
				for (unsigned i = 0; i < n; ++i) {
					if(!toExtend(n, emb, i)) continue;
					VertexId src = emb.get_vertex(i);
					for (auto e : graph->edges(src)) {
						GNode dst = graph->getEdgeDst(e);
						if (toAdd(n, emb, dst, i)) {
							if (n < max_size-1) {
								VertexEmbedding new_emb(emb);
								#ifdef USE_PID
								if (n == 2 && max_size == 4) new_emb.set_pid(find_motif_pattern_id(n, i, dst, emb));
								#endif
								new_emb.push_back(dst);
								out_queue.push_back(new_emb);
							} else {
								if (n < 4) {
									unsigned pid = find_motif_pattern_id(n, i, dst, emb);
									accumulators[pid] += 1;
								} else {
									#ifdef USE_MAP
									std::vector<bool> connected;
									get_connectivity(n, i, dst, emb, connected);
									StrQPattern qp(n+1, connected);
									if (qp_lmap->find(qp) != qp_lmap->end()) {
										(*qp_lmap)[qp] += 1;
										qp.clean();
									} else (*qp_lmap)[qp] = 1;
									#endif
								}
							}
						}
					}
				}
			},
			galois::chunk_size<CHUNK_SIZE>(), galois::steal(),
			galois::loopname("Extending")
		);
		//}, "ExtendingPapi");
	}
	inline void quick_reduce(unsigned n, unsigned i, VertexId dst, const VertexEmbedding &emb, StrQpMapFreq *qp_lmap) {
		std::vector<bool> connected;
		get_connectivity(n, i, dst, emb, connected);
		StrQPattern qp(n+1, connected);
		if (qp_lmap->find(qp) != qp_lmap->end()) {
			(*qp_lmap)[qp] += 1;
			qp.clean();
		} else (*qp_lmap)[qp] = 1;
	}
	// extension for vertex-induced motif
	inline void extend_vertex(unsigned level, EmbeddingList& emb_list) {
		UintList num_new_emb(emb_list.size());
		//UlongList num_new_emb(emb_list.size());
		//galois::runtime::profilePapi([&] () {
		galois::do_all(galois::iterate((size_t)0, emb_list.size()),
			[&](const size_t& pos) {
				unsigned n = level+1;
				#ifdef USE_MAP
				StrQpMapFreq *qp_lmap;
				if (n >= 4) qp_lmap = qp_localmaps.getLocal();
				#endif
				VertexEmbedding emb(n);
				get_embedding<VertexEmbedding>(level, pos, emb_list, emb);
				if (n < max_size-1) num_new_emb[pos] = 0;
				if (n == 3 && max_size == 4) emb.set_pid(emb_list.get_pid(pos));
				for (unsigned i = 0; i < n; ++i) {
					VertexId src = emb.get_vertex(i);
					for (auto e : graph->edges(src)) {
						GNode dst = graph->getEdgeDst(e);
						if (toAdd(n, emb, dst, i)) {
							if (n < max_size-1) {
								num_new_emb[pos] ++;
							} else {
								#ifdef USE_MAP
								//unsigned pid  = getPattern(n, i, dst, emb, pos);
								#ifdef USE_CUSTOM
								if (n < 4) {
									unsigned pid = find_motif_pattern_id(n, i, dst, emb, pos);
									accumulators[pid] += 1;
								} else
								#endif
								{
									quick_reduce(n, i, dst, emb, qp_lmap);
								}
								#endif
							}
						}
					}
				}
			},
			galois::chunk_size<CHUNK_SIZE>(), galois::steal(),
			galois::loopname("Extending-alloc")
		);
		//}, "ExtendingAllocPapi");
		if (level == max_size-2) return;
		UintList indices = parallel_prefix_sum<unsigned>(num_new_emb);
		//UlongList indices = parallel_prefix_sum<Ulong>(num_new_emb);
		num_new_emb.clear();
		auto new_size = indices.back();
		assert(new_size < 4294967296); // TODO: currently do not support vector size larger than 2^32
		std::cout << "number of new embeddings: " << new_size << "\n";
		emb_list.add_level(new_size);
		#ifdef USE_WEDGE
		if (level == 1 && max_size == 4) {
			is_wedge.resize(emb_list.size());
			std::fill(is_wedge.begin(), is_wedge.end(), 0);
		}
		#endif
		//galois::runtime::profilePapi([&] () {
		galois::do_all(galois::iterate((size_t)0, emb_list.size(level)),
			[&](const size_t& pos) {
				VertexEmbedding emb(level+1);
				get_embedding<VertexEmbedding>(level, pos, emb_list, emb);
				auto start = indices[pos];
				auto n = emb.size();
				for (unsigned i = 0; i < n; ++i) {
					VertexId src = emb.get_vertex(i);
					for (auto e : graph->edges(src)) {
						GNode dst = graph->getEdgeDst(e);
						//if (!is_vertexInduced_automorphism(n, emb, i, src, dst)) {
						if (toAdd(n, emb, dst, i)) {
							assert(start < indices.back());
							if (n == 2 && max_size == 4)
								emb_list.set_pid(start, find_motif_pattern_id(n, i, dst, emb, start));
							emb_list.set_idx(level+1, start, pos);
							emb_list.set_vid(level+1, start++, dst);
						}
					}
				}
			},
			galois::chunk_size<CHUNK_SIZE>(), galois::steal(),
			galois::loopname("Extending-insert")
		);
		//}, "ExtendingInsertPapi");
		indices.clear();
	}
	// extension for vertex-induced clique
	inline void extend_vertex_kcl(unsigned level, EmbeddingList& emb_list) {
		auto cur_size = emb_list.size();
		std::cout << "number of current embeddings: " << cur_size << "\n";
		UintList num_new_emb(cur_size);
		//galois::runtime::profilePapi([&] () {
		galois::do_all(galois::iterate((size_t)0, cur_size),
			[&](const size_t& pos) {
				BaseEmbedding emb(level+1);
				get_embedding<BaseEmbedding>(level, pos, emb_list, emb);
				VertexId vid = emb_list.get_vid(level, pos);
				num_new_emb[pos] = 0;
				for (auto e : graph->edges(vid)) {
					GNode dst = graph->getEdgeDst(e);
					if (toAdd(level+1, emb, dst, level)) {
						if (level < max_size-2) num_new_emb[pos] ++;
						else total_num += 1;
					}
				}
			},
			galois::chunk_size<CHUNK_SIZE>(), galois::steal(), galois::no_conflicts(),
			galois::loopname("Extending-alloc")
		);
		//}, "ExtendingAllocPapi");
		if (level == max_size-2) return;
		UintList indices = parallel_prefix_sum<unsigned>(num_new_emb);
		//UlongList indices = parallel_prefix_sum<Ulong>(num_new_emb);
		num_new_emb.clear();
		auto new_size = indices.back();
		std::cout << "number of new embeddings: " << new_size << "\n";
		assert(new_size < 4294967296); // TODO: currently do not support vector size larger than 2^32
		emb_list.add_level(new_size);
		//galois::runtime::profilePapi([&] () {
		galois::do_all(galois::iterate((size_t)0, emb_list.size(level)),
			[&](const size_t& pos) {
				BaseEmbedding emb(level+1);
				get_embedding<BaseEmbedding>(level, pos, emb_list, emb);
				VertexId vid = emb_list.get_vid(level, pos);
				unsigned start = indices[pos];
				for (auto e : graph->edges(vid)) {
					GNode dst = graph->getEdgeDst(e);
					if (toAdd(level+1, emb, dst, level)) {
						emb_list.set_idx(level+1, start, pos);
						emb_list.set_vid(level+1, start++, dst);
					}
				}
			},
			galois::chunk_size<CHUNK_SIZE>(), galois::steal(), galois::no_conflicts(),
			galois::loopname("Extending-insert")
		);
		//}, "ExtendingInsertPapi");
		indices.clear();
	}
	/*
	inline void reduce(VertexEmbeddingQueue &queue) {
		galois::do_all(galois::iterate(queue),
			[&](const VertexEmbedding& emb) {
				unsigned n = emb.size();
				for (unsigned i = 0; i < n; ++i) {
					VertexId src = emb.get_vertex(i);
					for (auto e : graph->edges(src)) {
						GNode dst = graph->getEdgeDst(e);
						if (!is_vertexInduced_automorphism(n, emb, i, src, dst)) {
							assert(n < 4);
							unsigned pid = find_motif_pattern_id(n, i, dst, emb);
							accumulators[pid] += 1;
						}
					}
				}
			},
			galois::chunk_size<CHUNK_SIZE>(), galois::steal(), galois::no_conflicts(),
			galois::wl<galois::worklists::PerSocketChunkFIFO<CHUNK_SIZE>>(), galois::loopname("Reduce")
		);
	}
	*/
	/*
	inline void reduce_lazy(VertexEmbeddingQueue &queue) {
		galois::do_all(galois::iterate(queue),
			[&](const VertexEmbedding& emb) {
				unsigned n = emb.size();
				//VertexEmbeddingBuffer emb_buffer; 
				//UintList src_buffer;
				UintList dst_buffer;
				UintList pos_buffer;
				for (unsigned i = 0; i < n; ++i) {
					VertexId src = emb.get_vertex(i);
					for (auto e : graph->edges(src)) {
						GNode dst = graph->getEdgeDst(e);
						//src_buffer.push_back(src);
						dst_buffer.push_back(dst);
						//VertexEmbedding new_emb(emb);
						//new_emb.push_back(dst);
						//emb_buffer.push_back(new_emb);
						pos_buffer.push_back(i);
					}
				}
				for (size_t i = 0; i < pos_buffer.size(); i++) {
					//VertexId src = src_buffer[i];
					VertexId dst = dst_buffer[i];
					VertexId pos = pos_buffer[i];
					VertexId src = emb.get_vertex(pos);
					//VertexId dst = emb_buffer[i].get_vertex(n);
					if (!is_vertexInduced_automorphism(n, emb, pos, src, dst)) {
						assert(n < 4);
						unsigned pid = find_motif_pattern_id(n, pos, dst, emb);
						accumulators[pid] += 1;
					}
				}
			},
			galois::chunk_size<CHUNK_SIZE>(), galois::steal(), galois::no_conflicts(),
			galois::loopname("Reduce")
		);
	}
	//*/
	/*
	inline void reduce(unsigned level, EmbeddingList& emb_list) {
		//galois::runtime::profileVtune([&] () {
		galois::do_all(galois::iterate((size_t)0, emb_list.size(level)),
			[&](const size_t& pos) {
				VertexEmbedding emb(level+1);
				get_embedding<VertexEmbedding>(level, pos, emb_list, emb);
				unsigned n = emb.size();
				if (n == 3) emb.set_pid(emb_list.get_pid(pos));
				for (unsigned i = 0; i < n; ++i) {
					VertexId src = emb.get_vertex(i);
					for (auto e : graph->edges(src)) {
						GNode dst = graph->getEdgeDst(e);
						if (!is_vertexInduced_automorphism(n, emb, i, src, dst)) {
							assert(n < 4);
							unsigned pid = find_motif_pattern_id(n, i, dst, emb, pos);
							accumulators[pid] += 1;
						}
					}
				}
				emb.clean();
			},
			galois::chunk_size<CHUNK_SIZE>(), galois::steal(), galois::no_conflicts(),
			galois::wl<galois::worklists::PerSocketChunkFIFO<CHUNK_SIZE>>(),
			galois::loopname("Reduce")
		);
		//}, "ReduceVtune");
	}
	// quick pattern reduction 
	inline void quick_reduce(VertexEmbeddingQueue &queue) {
		for (auto i = 0; i < numThreads; i++) qp_localmaps.getLocal(i)->clear();
		galois::do_all(galois::iterate(queue),
			[&](const VertexEmbedding& emb) {
				StrQpMapFreq *qp_map = qp_localmaps.getLocal();
				unsigned n = emb.size();
				for (unsigned i = 0; i < n; ++i) {
					VertexId src = emb.get_vertex(i);
					for (auto e : graph->edges(src)) {
						GNode dst = graph->getEdgeDst(e);
						if (!is_vertexInduced_automorphism(n, emb, i, src, dst)) {
							std::vector<bool> connected;
							get_connectivity(n, i, dst, emb, connected);
							StrQPattern qp(n+1, connected);
							if (qp_map->find(qp) != qp_map->end()) {
								(*qp_map)[qp] += 1;
								qp.clean();
							} else (*qp_map)[qp] = 1;
						}
					}
				}
			},
			galois::chunk_size<CHUNK_SIZE>(), galois::steal(),
			galois::no_conflicts(), galois::wl<galois::worklists::PerSocketChunkFIFO<CHUNK_SIZE>>(),
			galois::loopname("QuickReduction")
		);
	}
	inline void quick_reduce(unsigned level, const EmbeddingList& emb_list) {
		for (auto i = 0; i < numThreads; i++) qp_localmaps.getLocal(i)->clear();
		galois::do_all(galois::iterate((size_t)0, emb_list.size(level)),
			[&](const size_t& pos) {
				StrQpMapFreq* qp_map = qp_localmaps.getLocal();
				VertexEmbedding emb(level+1);
				get_embedding<VertexEmbedding>(level, pos, emb_list, emb);
				unsigned n = emb.size();
				for (unsigned i = 0; i < n; ++i) {
					VertexId src = emb.get_vertex(i);
					for (auto e : graph->edges(src)) {
						GNode dst = graph->getEdgeDst(e);
						if (!is_vertexInduced_automorphism(n, emb, i, src, dst)) {
							std::vector<bool> connected;
							get_connectivity(n, i, dst, emb, connected);
							StrQPattern qp(n+1, connected);
							if (qp_map->find(qp) != qp_map->end()) {
								(*qp_map)[qp] += 1;
								qp.clean();
							} else (*qp_map)[qp] = 1;
						}
					}
				}
				emb.clean();
			},
			galois::chunk_size<CHUNK_SIZE>(), galois::steal(),
			galois::no_conflicts(), galois::wl<galois::worklists::PerSocketChunkFIFO<CHUNK_SIZE>>(),
			galois::loopname("QuickReduction")
		);
	}
	//*/
	// canonical pattern reduction
	inline void canonical_reduce() {
		for (auto i = 0; i < numThreads; i++) cg_localmaps.getLocal(i)->clear();
		galois::do_all(galois::iterate(qp_map),
			[&](auto& element) {
				StrCgMapFreq* cg_map = cg_localmaps.getLocal();
				StrCPattern cg(element.first);
				if (cg_map->find(cg) != cg_map->end()) (*cg_map)[cg] += element.second;
				else (*cg_map)[cg] = element.second;
				cg.clean();

			},
			galois::chunk_size<CHUNK_SIZE>(), galois::steal(),
			galois::no_conflicts(), galois::wl<galois::worklists::PerSocketChunkFIFO<CHUNK_SIZE>>(),
			galois::loopname("CanonicalReduction")
		);
		qp_map.clear();
	}
	inline void merge_qp_map() {
		qp_map.clear();
		for (unsigned i = 0; i < qp_localmaps.size(); i++) {
			StrQpMapFreq qp_lmap = *qp_localmaps.getLocal(i);
			for (auto element : qp_lmap) {
				if (qp_map.find(element.first) != qp_map.end())
					qp_map[element.first] += element.second;
				else qp_map[element.first] = element.second;
			}
		}
	}
	inline void merge_cg_map() {
		cg_map.clear();
		for (unsigned i = 0; i < cg_localmaps.size(); i++) {
			StrCgMapFreq cg_lmap = *cg_localmaps.getLocal(i);
			for (auto element : cg_lmap) {
				if (cg_map.find(element.first) != cg_map.end())
					cg_map[element.first] += element.second;
				else cg_map[element.first] = element.second;
			}
		}
	}

	// Utilities
	//inline unsigned get_total_num_cliques() { return num_cliques; }
	Ulong get_total_count() { return total_num.reduce(); }
	void printout_motifs() {
		std::cout << std::endl;
		if (accumulators.size() == 2) {
			std::cout << "\ttriangles\t" << accumulators[0].reduce() << std::endl;
			std::cout << "\t3-chains\t" << accumulators[1].reduce() << std::endl;
		} else if (accumulators.size() == 6) {
			std::cout << "\t4-paths --> " << accumulators[0].reduce() << std::endl;
			std::cout << "\t3-stars --> " << accumulators[1].reduce() << std::endl;
			std::cout << "\t4-cycles --> " << accumulators[2].reduce() << std::endl;
			std::cout << "\ttailed-triangles --> " << accumulators[3].reduce() << std::endl;
			std::cout << "\tdiamonds --> " << accumulators[4].reduce() << std::endl;
			std::cout << "\t4-cliques --> " << accumulators[5].reduce() << std::endl;
		} else {
			if (max_size < 9) {
				std::cout << std::endl;
				//for (auto it = p_map.begin(); it != p_map.end(); ++it)
				//	std::cout << "{" << it->first << "} --> " << it->second << std::endl;
			} else {
				std::cout << std::endl;
				for (auto it = cg_map.begin(); it != cg_map.end(); ++it)
					std::cout << it->first << " --> " << it->second << std::endl;
			}
		}
		std::cout << std::endl;
	}

private:
	int npatterns;
	unsigned max_size;
	UlongAccu total_num;
	std::vector<UlongAccu> accumulators;
	std::vector<unsigned> is_wedge; // indicate a 3-vertex embedding is a wedge or chain (v0-cntered or v1-centered)
	StrQpMapFreq qp_map; // quick patterns map for counting the frequency
	StrCgMapFreq cg_map; // canonical graph map for couting the frequency
	LocalStrQpMapFreq qp_localmaps; // quick patterns local map for each thread
	LocalStrCgMapFreq cg_localmaps; // canonical graph local map for each thread
	galois::substrate::SimpleLock slock;

	template <typename EmbeddingTy>
	inline void get_embedding(unsigned level, unsigned pos, const EmbeddingList& emb_list, EmbeddingTy &emb) {
		VertexId vid = emb_list.get_vid(level, pos);
		IndexTy idx = emb_list.get_idx(level, pos);
		ElementType ele(vid);
		emb.set_element(level, ele);
		// backward constructing the embedding
		for (unsigned l = 1; l < level; l ++) {
			VertexId u = emb_list.get_vid(level-l, idx);
			ElementType ele(u);
			emb.set_element(level-l, ele);
			idx = emb_list.get_idx(level-l, idx);
		}
		ElementType ele0(idx);
		emb.set_element(0, ele0);
	}

protected:
	template <typename EmbeddingTy = VertexEmbedding>
	inline bool is_vertexInduced_automorphism(unsigned n, const EmbeddingTy& emb, unsigned idx, VertexId src, VertexId dst) {
		//unsigned n = emb.size();
		// the new vertex id should be larger than the first vertex id
		if (dst <= emb.get_vertex(0)) return true;
		// the new vertex should not already exist in the embedding
		for (unsigned i = 1; i < n; ++i)
			if (dst == emb.get_vertex(i)) return true;
		// the new vertex should not already be extended by any previous vertex in the embedding
		for (unsigned i = 0; i < idx; ++i)
			if (is_connected(emb.get_vertex(i), dst)) return true;
		// the new vertex id should be larger than any vertex id after its source vertex in the embedding
		for (unsigned i = idx+1; i < n; ++i)
			if (dst < emb.get_vertex(i)) return true;
		return false;
	}

	inline unsigned find_motif_pattern_id(unsigned n, unsigned idx, VertexId dst, const VertexEmbedding& emb, unsigned pos = 0) {
		unsigned pid = 0;
		if (n == 2) { // count 3-motifs
			pid = 1; // 3-chain
			if (idx == 0) {
				if (is_connected(emb.get_vertex(1), dst)) pid = 0; // triangle
				#ifdef USE_WEDGE
				else if (max_size == 4) is_wedge[pos] = 1; // wedge; used for 4-motif
				#endif
			}
		} else if (n == 3) { // count 4-motifs
			unsigned num_edges = 1;
			pid = emb.get_pid();
			if (pid == 0) { // extending a triangle
				for (unsigned j = idx+1; j < n; j ++)
					if (is_connected(emb.get_vertex(j), dst)) num_edges ++;
				pid = num_edges + 2; // p3: tailed-triangle; p4: diamond; p5: 4-clique
			} else { // extending a 3-chain
				assert(pid == 1);
				std::vector<bool> connected(3, false);
				connected[idx] = true;
				for (unsigned j = idx+1; j < n; j ++) {
					if (is_connected(emb.get_vertex(j), dst)) {
						num_edges ++;
						connected[j] = true;
					}
				}
				if (num_edges == 1) {
					pid = 0; // p0: 3-path
					unsigned center = 1;
					#ifdef USE_WEDGE
					if (is_wedge[pos]) center = 0;
					#else
					center = is_connected(emb.get_vertex(1), emb.get_vertex(2)) ? 1 : 0;
					#endif
					if (idx == center) pid = 1; // p1: 3-star
				} else if (num_edges == 2) {
					pid = 2; // p2: 4-cycle
					unsigned center = 1;
					#ifdef USE_WEDGE
					if (is_wedge[pos]) center = 0;
					#else
					center = is_connected(emb.get_vertex(1), emb.get_vertex(2)) ? 1 : 0;
					#endif
					if (connected[center]) pid = 3; // p3: tailed-triangle
				} else {
					pid = 4; // p4: diamond
				}
			}
		} else { // count 5-motif and beyond
			std::vector<bool> connected;
			get_connectivity(n, idx, dst, emb, connected);
			Matrix A(n+1, std::vector<MatType>(n+1, 0));
			gen_adj_matrix(n+1, connected, A);
			std::vector<MatType> c(n+1, 0);
			char_polynomial(n+1, A, c);
			bliss::UintSeqHash h;
			for (unsigned i = 0; i < n+1; ++i)
				h.update((unsigned)c[i]);
			pid = h.get_value();
		}
		return pid;
	}
};

#endif // VERTEX_MINER_HPP_
