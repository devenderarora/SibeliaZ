#ifndef _TRASERVAL_H_
#define _TRAVERSAL_H_

//#define _DEBUG_OUT_

#include <set>
#include <map>
#include <list>
#include <ctime>
#include <queue>
#include <iterator>
#include <cassert>
#include <numeric>
#include <sstream>
#include <iostream>
#include <functional>
#include <unordered_map>

#include <tbb/parallel_for.h>

#include "path.h"

namespace Sibelia
{
	extern const std::string DELIMITER;
	extern const std::string VERSION;

	class BlockInstance
	{
	public:
		BlockInstance() {}
		BlockInstance(int id, const size_t chr, size_t start, size_t end) : id_(id), chr_(chr), start_(start), end_(end) {}
		void Reverse();
		int GetSignedBlockId() const;
		bool GetDirection() const;
		int GetBlockId() const;
		int GetSign() const;
		size_t GetChrId() const;
		size_t GetStart() const;
		size_t GetEnd() const;
		size_t GetLength() const;
		size_t GetConventionalStart() const;
		size_t GetConventionalEnd() const;
		std::pair<size_t, size_t> CalculateOverlap(const BlockInstance & instance) const;
		bool operator < (const BlockInstance & toCompare) const;
		bool operator == (const BlockInstance & toCompare) const;
		bool operator != (const BlockInstance & toCompare) const;
	private:
		int id_;
		size_t start_;
		size_t end_;
		size_t chr_;
	};

	namespace
	{
		const bool COVERED = true;
		typedef std::vector<BlockInstance> BlockList;
		typedef std::pair<size_t, std::vector<BlockInstance> > GroupedBlock;
		typedef std::vector<GroupedBlock> GroupedBlockList;
		bool ByFirstElement(const GroupedBlock & a, const GroupedBlock & b)
		{
			return a.first < b.first;
		}

		std::string IntToStr(size_t x)
		{
			std::stringstream ss;
			ss << x;
			return ss.str();
		}

		template<class Iterator1, class Iterator2>
		void CopyN(Iterator1 it, size_t count, Iterator2 out)
		{
			for (size_t i = 0; i < count; i++)
			{
				*out++ = *it++;
			}
		}

		template<class Iterator>
		Iterator AdvanceForward(Iterator it, size_t step)
		{
			std::advance(it, step);
			return it;
		}

		template<class Iterator>
		Iterator AdvanceBackward(Iterator it, size_t step)
		{
			for (size_t i = 0; i < step; i++)
			{
				--it;
			}

			return it;
		}


		typedef std::pair<size_t, size_t> IndexPair;
		template<class T, class F, class It>
		void GroupBy(std::vector<T> & store, F pred, It out)
		{
			sort(store.begin(), store.end(), pred);
			for (size_t now = 0; now < store.size();)
			{
				size_t prev = now;
				for (; now < store.size() && !pred(store[prev], store[now]); now++);
				*out++ = std::make_pair(prev, now);
			}
		}

		template<class F>
		bool CompareBlocks(const BlockInstance & a, const BlockInstance & b, F f)
		{
			return (a.*f)() < (b.*f)();
		}

		template<class F>
		bool EqualBlocks(const BlockInstance & a, const BlockInstance & b, F f)
		{
			return f(a) == f(b);
		}

		template<class Iterator, class F, class ReturnType>
		struct FancyIterator : public std::iterator<std::forward_iterator_tag, ReturnType>
		{
		public:
			FancyIterator& operator++()
			{
				++it;
				return *this;
			}

			FancyIterator operator++(int)
			{
				FancyIterator ret(*this);
				++(*this);
				return ret;
			}

			bool operator == (FancyIterator toCompare) const
			{
				return it == toCompare.it;
			}

			bool operator != (FancyIterator toCompare) const
			{
				return !(*this == toCompare);
			}

			ReturnType operator * ()
			{
				return f(*it);
			}

			FancyIterator() {}
			FancyIterator(Iterator it, F f) : it(it), f(f) {}

		private:
			F f;
			Iterator it;
		};

		template<class Iterator, class F, class ReturnType>
		FancyIterator<Iterator, F, ReturnType> CFancyIterator(Iterator it, F f, ReturnType)
		{
			return FancyIterator<Iterator, F, ReturnType>(it, f);
		}

	}

	bool compareById(const BlockInstance & a, const BlockInstance & b);
	bool compareByChrId(const BlockInstance & a, const BlockInstance & b);
	bool compareByStart(const BlockInstance & a, const BlockInstance & b);

	void CreateOutDirectory(const std::string & path);

	class BlocksFinder
	{
	public:

		BlocksFinder(JunctionStorage & storage, size_t k) : storage_(storage), k_(k)
		{
			progressCount_ = 50;
			scoreFullChains_ = true;
		}

		static bool DegreeCompare(const JunctionStorage & storage, int64_t v1, int64_t v2)
		{
			return storage.GetInstancesCount(v1) > storage.GetInstancesCount(v2);
		}

		void Split(std::string & source, std::vector<std::string> & result)
		{
			std::stringstream ss;
			ss << source;
			result.clear();
			while (ss >> source)
			{
				result.push_back(source);
			}
		}
		
		void FindBlocks(int64_t minBlockSize, int64_t maxBranchSize, int64_t maxFlankingSize, int64_t lookingDepth, int64_t sampleSize, int64_t threads, const std::string & debugOut)
		{
			blocksFound_ = 0;
			minBlockSize_ = minBlockSize;
			maxBranchSize_ = maxBranchSize;
			maxFlankingSize_ = maxFlankingSize;
			blockId_.resize(storage_.GetChrNumber());
			for (int64_t i = 0; i < storage_.GetChrNumber(); i++)
			{
				blockId_[i].resize(storage_.GetChrVerticesCount(i));
			}

			std::vector<int64_t> shuffle;
			for (int64_t v = -storage_.GetVerticesNumber() + 1; v < storage_.GetVerticesNumber(); v++)
			{
				for (JunctionStorage::JunctionIterator it(v); it.Valid(); ++it)
				{
					if (it.IsPositiveStrand())
					{
						shuffle.push_back(v);
						break;
					}
				}
			}

			using namespace std::placeholders;
			tbb::task_scheduler_init init(static_cast<int>(threads));

			time_t mark = time(0);
			count_ = 0;
			/*
			std::cout << '[' << std::flush;
			progressPortion_ = shuffle.size() / progressCount_;

			tbb::parallel_for(tbb::blocked_range<size_t>(0, shuffle.size()), ProcessVertex(*this, shuffle));
			std::cout << ']' << std::endl;
			*/
			starter_ = 0;
			tbb::parallel_for(tbb::blocked_range<size_t>(0, shuffle.size()), CheckIfSource(*this, shuffle));
			//std::cout << "Time: " << time(0) - mark << std::endl;
			size_t totalMarks = 0;
			for (size_t i = 0; i < storage_.GetChrNumber(); i++)
			{
				totalMarks += storage_.GetChrVerticesCount(i);
			}
			
			FindBlocksPwise();
		}

		void FindBlocksPwise()
		{
			std::sort(sink_.begin(), sink_.end());
			for (auto & u : source_)
			{
				auto v = *std::lower_bound(sink_.begin(), sink_.end(), u);
				//std::cout << u.ToString() << std::endl << v.ToString() << std::endl << std::endl;
				for (size_t l = 0; l < 2; l++)
				{
					if (u.branch[l].GetChrId() != v.branch[l].GetChrId())
					{
						continue;
					}
				}

				if (ChainLength(u, v) >= minBlockSize_)
				{
					tbb::mutex::scoped_lock lock(globalMutex_);
					int64_t currentBlock = ++blocksFound_;
					for (size_t l = 0; l < 2; l++)
					{
						auto it = u.branch[l];
						auto jt = v.branch[l];
						if (jt.IsPositiveStrand())
						{
							blocksInstance_.push_back(BlockInstance(+currentBlock, jt.GetChrId(), it.GetPosition(), jt.GetPosition() + k_));
						}
						else
						{
							blocksInstance_.push_back(BlockInstance(-currentBlock, jt.GetChrId(), jt.GetPosition() - k_, it.GetPosition()));
						}
					}
				}
			}
		}

		void ListBlocksSequences(const BlockList & block, const std::string & directory) const
		{
			std::vector<IndexPair> group;
			BlockList blockList = block;
			GroupBy(blockList, compareById, std::back_inserter(group));
			for (std::vector<IndexPair>::iterator it = group.begin(); it != group.end(); ++it)
			{
				std::ofstream out;
				std::stringstream ss;
				ss << directory << "/" << blockList[it->first].GetBlockId() << ".fa";
				TryOpenFile(ss.str(), out);
				for (size_t block = it->first; block < it->second; block++)
				{
					size_t length = blockList[block].GetLength();
					size_t chr = blockList[block].GetChrId();
					size_t chrSize = storage_.GetChrSequence(chr).size();
					out << ">" << blockList[block].GetBlockId() << "_" << block - it->first << " ";
					out << storage_.GetChrDescription(chr) << ";";
					if (blockList[block].GetSignedBlockId() > 0)
					{
						out << blockList[block].GetStart() << ";" << length << ";" << "+;" << chrSize << std::endl;
						OutputLines(storage_.GetChrSequence(chr).begin() + blockList[block].GetStart(), length, out);
					}
					else
					{
						size_t start = chrSize - blockList[block].GetEnd();
						out << start << ";" << length << ";" << "-;" << chrSize << std::endl;
						std::string::const_reverse_iterator it(storage_.GetChrSequence(chr).begin() + blockList[block].GetEnd());
						OutputLines(CFancyIterator(it, TwoPaCo::DnaChar::ReverseChar, ' '), length, out);
					}

					out << std::endl;
				}
			}
		}

		void GenerateOutput(const std::string & outDir, bool genSeq)
		{
			const auto & trimmedBlocks = blocksInstance_;
			std::vector<std::vector<bool> > covered(storage_.GetChrNumber());
			for (size_t i = 0; i < covered.size(); i++)
			{
				covered[i].assign(storage_.GetChrSequence(i).size() + 1, false);
			}

			for (auto & b : blocksInstance_)
			{
				for (size_t i = b.GetStart(); i < b.GetEnd(); i++)
				{
					covered[b.GetChrId()][i] = true;
				}
			}

			size_t total = 0;
			size_t totalCovered = 0;
			for (auto & chr : covered)
			{
				total += chr.size();
				totalCovered += std::count(chr.begin(), chr.end(), true);
			}

			std::cout.setf(std::cout.fixed);
			std::cout.precision(2);
			std::cout << "Blocks found: " << blocksFound_ << std::endl;
			std::cout << "Coverage: " << double(totalCovered) / total << std::endl;

			CreateOutDirectory(outDir);
			std::string blocksDir = outDir + "/blocks";
			ListBlocksIndicesGFF(trimmedBlocks, outDir + "/" + "blocks_coords.gff");
			if (genSeq)
			{
				CreateOutDirectory(blocksDir);
				ListBlocksSequences(trimmedBlocks, blocksDir);
			}
		}


	private:

		template<class Iterator>
		void OutputLines(Iterator start, size_t length, std::ostream & out) const
		{
			for (size_t i = 1; i <= length; i++, ++start)
			{
				out << *start;
				if (i % 80 == 0 && i != length)
				{
					out << std::endl;
				}
			}
		}

		void ListBlocksIndicesGFF(const BlockList & blockList, const std::string & fileName) const;
		void TryOpenFile(const std::string & fileName, std::ofstream & stream) const;

		template<class T>
		void DumpVertex(int64_t id, std::ostream & out, T & visit, int64_t cnt = 5) const
		{
			for (auto kt = JunctionStorage::JunctionIterator(id); kt.Valid(); ++kt)
			{
				auto jt = kt.SequentialIterator();
				for (int64_t i = 0; i < cnt; i++)
				{
					auto it = jt - 1;
					auto pr = std::make_pair(it, jt);
					if (it.Valid() && std::find(visit.begin(), visit.end(), pr) == visit.end())
					{
						int64_t length = it.GetPosition() - jt.GetPosition();
						out << it.GetVertexId() << " -> " << jt.GetVertexId()
							<< "[label=\"" << it.GetChar() << ", " << it.GetChrId() << ", " << it.GetPosition() << "," << length << "\""
							<< (it.IsPositiveStrand() ? "color=blue" : "color=red") << "]\n";
						visit.push_back(pr);
					}

					jt = it;
				}
			}

			for (auto kt = JunctionStorage::JunctionIterator(id); kt.Valid(); ++kt)
			{
				auto it = kt.SequentialIterator();
				for (int64_t i = 0; i < cnt; i++)
				{
					auto jt = it + 1;
					auto pr = std::make_pair(it, jt);
					if (jt.Valid() && std::find(visit.begin(), visit.end(), pr) == visit.end())
					{
						int64_t length = it.GetPosition() - jt.GetPosition();
						out << it.GetVertexId() << " -> " << jt.GetVertexId()
							<< "[label=\"" << it.GetChar() << ", " << it.GetChrId() << ", " << it.GetPosition() << "," << length << "\""
							<< (it.IsPositiveStrand() ? "color=blue" : "color=red") << "]\n";
						visit.push_back(pr);
					}

					it = jt;
				}
			}
		}

		struct BranchData
		{
			std::vector<size_t> branchId;
		};

		typedef std::vector<std::vector<size_t> > BubbledBranches;

		struct Fork
		{
			Fork(JunctionStorage::JunctionSequentialIterator it, JunctionStorage::JunctionSequentialIterator jt)
			{
				if (it < jt)
				{
					branch[0] = it;
					branch[1] = jt;
				}
				else
				{
					branch[0] = jt;
					branch[1] = it;
				}
			}

			bool operator == (const Fork & f) const
			{
				return branch[0] == f.branch[0] && branch[1] == f.branch[1];
			}

			bool operator != (const Fork & f) const
			{
				return !(*this == f);
			}

			std::string ToString() const
			{
				std::stringstream ss;
				for (size_t l = 0; l < 2; l++)
				{
					ss << branch[l].GetChrId() << ' ' << branch[l].GetPosition() << ' ';
				}

				return ss.str();
			}

			bool operator < (const Fork & f) const
			{
				for (size_t l = 0; l < 2; l++)
				{
					if (branch[l].IsPositiveStrand() != f.branch[l].IsPositiveStrand())
					{
						return branch[l].IsPositiveStrand() < f.branch[l].IsPositiveStrand();
					}
				}

				for (size_t l = 0; l < 2; l++)
				{
					if (branch[l].GetChrId() != f.branch[l].GetChrId())
					{
						return branch[l].GetChrId() < f.branch[l].GetChrId();
					}
				}
				
				for (size_t l = 0; l < 2; l++)
				{
					if (branch[l].IsPositiveStrand())
					{
						return branch[l].GetPosition() < f.branch[l].GetPosition();
					}
					else
					{
						return branch[l].GetPosition() > f.branch[l].GetPosition();
					}
				}
			}

			JunctionStorage::JunctionSequentialIterator branch[2];
		};

		int64_t ChainLength(const Fork & now, const Fork & next) const
		{
			return min(abs(now.branch[0].GetPosition() - next.branch[0].GetPosition()), abs(now.branch[1].GetPosition() - next.branch[1].GetPosition()));
		}

		void BubbledBranchesForward(int64_t vertexId, const std::vector<JunctionStorage::JunctionSequentialIterator> & instance, BubbledBranches & bulges) const
		{
			std::vector<size_t> parallelEdge[5];
			std::map<int64_t, BranchData> visit;
			bulges.assign(instance.size(), std::vector<size_t>());
			for (size_t i = 0; i < instance.size(); i++)
			{
				auto vertex = instance[i];
				if ((vertex + 1).Valid())
				{
					parallelEdge[TwoPaCo::DnaChar::MakeUpChar(vertex.GetChar())].push_back(i);
				}

				for (int64_t startPosition = vertex++.GetPosition(); vertex.Valid() && abs(startPosition - vertex.GetPosition()) <= maxBranchSize_; ++vertex)
				{
					int64_t nowVertexId = vertex.GetVertexId();
					auto point = visit.find(nowVertexId);
					if (point == visit.end())
					{
						BranchData bData;
						bData.branchId.push_back(i);
						visit[nowVertexId] = bData;
					}
					else
					{
						point->second.branchId.push_back(i);
					}
				}
			}

			for (size_t i = 0; i < 5; i++)
			{
				for (size_t j = 0; j < parallelEdge[i].size(); j++)
				{
					for (size_t k = j + 1; k < parallelEdge[i].size(); k++)
					{
						size_t smallBranch = parallelEdge[i][j];
						size_t largeBranch = parallelEdge[i][k];
						bulges[smallBranch].push_back(largeBranch);
					}
				}
			}

			for (auto point = visit.begin(); point != visit.end(); ++point)
			{
				std::sort(point->second.branchId.begin(), point->second.branchId.end());
				for (size_t j = 0; j < point->second.branchId.size(); j++)
				{
					for (size_t k = j + 1; k < point->second.branchId.size(); k++)
					{
						size_t smallBranch = point->second.branchId[j];
						size_t largeBranch = point->second.branchId[k];
						if (smallBranch != largeBranch && std::find(bulges[smallBranch].begin(), bulges[smallBranch].end(), largeBranch) == bulges[smallBranch].end())
						{
							bulges[smallBranch].push_back(largeBranch);
						}
					}
				}
			}
		}

		void BubbledBranchesBackward(int64_t vertexId, const std::vector<JunctionStorage::JunctionSequentialIterator> & instance, BubbledBranches & bulges) const
		{
			std::vector<size_t> parallelEdge[5];
			std::map<int64_t, BranchData> visit;
			bulges.assign(instance.size(), std::vector<size_t>());
			for (size_t i = 0; i < instance.size(); i++)
			{
				auto vertex = instance[i];
				auto prev = vertex - 1;
				if (prev.Valid())
				{
					parallelEdge[TwoPaCo::DnaChar::MakeUpChar(prev.GetChar())].push_back(i);
				}

				for (int64_t startPosition = vertex--.GetPosition(); vertex.Valid() && abs(startPosition - vertex.GetPosition()) <= maxBranchSize_; --vertex)
				{
					int64_t nowVertexId = vertex.GetVertexId();
					auto point = visit.find(nowVertexId);
					if (point == visit.end())
					{
						BranchData bData;
						bData.branchId.push_back(i);
						visit[nowVertexId] = bData;
					}
					else
					{
						point->second.branchId.push_back(i);
					}
				}
			}

			for (size_t i = 0; i < 5; i++)
			{
				for (size_t j = 0; j < parallelEdge[i].size(); j++)
				{
					for (size_t k = j + 1; k < parallelEdge[i].size(); k++)
					{
						size_t smallBranch = parallelEdge[i][j];
						size_t largeBranch = parallelEdge[i][k];
						bulges[smallBranch].push_back(largeBranch);
					}
				}
			}

			for (auto point = visit.begin(); point != visit.end(); ++point)
			{
				std::sort(point->second.branchId.begin(), point->second.branchId.end());
				for (size_t j = 0; j < point->second.branchId.size(); j++)
				{
					for (size_t k = j + 1; k < point->second.branchId.size(); k++)
					{
						size_t smallBranch = point->second.branchId[j];
						size_t largeBranch = point->second.branchId[k];
						if (smallBranch != largeBranch && std::find(bulges[smallBranch].begin(), bulges[smallBranch].end(), largeBranch) == bulges[smallBranch].end())
						{
							bulges[smallBranch].push_back(largeBranch);
						}
					}
				}
			}
		}

		struct CheckIfSource
		{
		public:
			BlocksFinder & finder;
			std::vector<int64_t> & shuffle;

			CheckIfSource(BlocksFinder & finder, std::vector<int64_t> & shuffle) : finder(finder), shuffle(shuffle)
			{
			}

			void operator()(tbb::blocked_range<size_t> & range) const
			{
				std::vector<uint32_t> data;
				std::vector<uint32_t> count(finder.storage_.GetVerticesNumber() * 2 + 1, 0);
				std::pair<int64_t, std::vector<Path::Instance> > goodInstance;
				Path finalizer(finder.storage_, finder.maxBranchSize_, finder.minBlockSize_, finder.minBlockSize_, finder.maxFlankingSize_);
				Path currentPath(finder.storage_, finder.maxBranchSize_, finder.minBlockSize_, finder.minBlockSize_, finder.maxFlankingSize_);

				BubbledBranches forwardBubble;
				BubbledBranches backwardBubble;
				std::vector<JunctionStorage::JunctionSequentialIterator> instance;
				for (size_t r = range.begin(); r != range.end(); r++)
				{
					if (finder.count_++ % 10000 == 0)
					{
						tbb::mutex::scoped_lock lock(finder.globalMutex_);
						std::cout << finder.count_ << '\t' << shuffle.size() << std::endl;
					}

					instance.clear();
					int64_t vertex = shuffle[r];
					for (auto it = JunctionStorage::JunctionIterator(vertex); it.Valid(); ++it)
					{
						instance.push_back(it.SequentialIterator());
					}

					finder.BubbledBranchesForward(vertex, instance, forwardBubble);
					finder.BubbledBranchesBackward(vertex, instance, backwardBubble);
					for (size_t i = 0; i < forwardBubble.size(); i++)
					{
						for (size_t j = 0; j < forwardBubble[i].size(); j++)
						{
							size_t k = forwardBubble[i][j];
							if (std::find(backwardBubble[i].begin(), backwardBubble[i].end(), k) == backwardBubble[i].end() && (instance[i].IsPositiveStrand() || instance[k].IsPositiveStrand()))
							{
								tbb::mutex::scoped_lock lock(finder.globalMutex_);
								finder.source_.push_back(Fork(instance[i], instance[k]));
							}
						}
					}

					for (size_t i = 0; i < backwardBubble.size(); i++)
					{
						for (size_t j = 0; j < backwardBubble[i].size(); j++)
						{
							size_t k = backwardBubble[i][j];
							if (std::find(forwardBubble[i].begin(), forwardBubble[i].end(), k) == forwardBubble[i].end() && (instance[i].IsPositiveStrand() || instance[k].IsPositiveStrand()))
							{
								tbb::mutex::scoped_lock lock(finder.globalMutex_);
								finder.sink_.push_back(Fork(instance[i], instance[k]));
							}
						}
					}

				}
			}
		};


		template<class T>
		static void AddIfNotExists(std::vector<T> & adj, T value)
		{
			if (std::find(adj.begin(), adj.end(), value) == adj.end())
			{
				adj.push_back(value);
			}
		}


		struct NextVertex
		{
			int64_t diff;
			int64_t count;
			JunctionStorage::JunctionSequentialIterator origin;
			NextVertex() : count(0)
			{

			}

			NextVertex(int64_t diff, JunctionStorage::JunctionSequentialIterator origin) : origin(origin), diff(diff), count(1)
			{

			}
		};

		int64_t k_;
		size_t progressCount_;
		size_t progressPortion_;
		std::atomic<int64_t> count_;
		std::atomic<int64_t> starter_;
		std::atomic<int64_t> blocksFound_;
		std::vector<size_t> pointComponent_;


		int64_t scalingFactor_;
		bool scoreFullChains_;
		int64_t minBlockSize_;
		int64_t maxBranchSize_;
		int64_t maxFlankingSize_;
		JunctionStorage & storage_;
		tbb::mutex progressMutex_;
		tbb::mutex globalMutex_;
		std::ofstream debugOut_;
		std::vector<BlockInstance> blocksInstance_;
		std::vector<std::vector<Assignment> > blockId_;

		std::vector<Fork> sink_;
		std::vector<Fork> source_;

#ifdef _DEBUG_OUT_
		bool debug_;
		std::set<int64_t> missingVertex_;
#endif
	};
}

#endif
