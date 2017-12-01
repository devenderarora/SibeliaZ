#ifndef _PATH_H_
#define _PATH_H_

#include <set>
#include <cassert>
#include <algorithm>
#include "distancekeeper.h"


#include <tbb/mutex.h>
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>
#include <tbb/task_scheduler_init.h>

namespace Sibelia
{
	struct Assignment
	{
		int32_t block;
		int32_t instance;
		Assignment()
		{

		}

		bool operator == (const Assignment & assignment) const
		{
			return block == assignment.block && instance == assignment.instance;
		}
	};

	struct BestPath;

	struct Path
	{
	public:
		Path(const JunctionStorage & storage,			
			int64_t maxBranchSize,
			int64_t minBlockSize,
			int64_t maxFlankingSize,
			bool checkConsistency = false) :
			maxBranchSize_(maxBranchSize), minBlockSize_(minBlockSize), maxFlankingSize_(maxFlankingSize), storage_(&storage),
			minChainSize_(minBlockSize - 2 * maxFlankingSize), distanceKeeper_(storage.GetVerticesNumber())
		{
			
		}

		void Init(int64_t vid)
		{
			origin_ = vid;			
			distanceKeeper_.Set(vid, 0);
			leftBodyFlank_ = rightBodyFlank_ = 0;
			for (size_t i = 0; i < storage_->GetInstancesCount(vid); i++)
			{
				JunctionStorage::JunctionIterator it = storage_->GetJunctionInstance(vid, i);
				if (!it.IsUsed())
				{
					instance_.insert(Instance(it, 0));
				}
			}
		}

		bool IsInPath(int64_t vertex) const
		{
			return distanceKeeper_.IsSet(vertex);
		}

		struct Instance
		{	
		private:
			int64_t frontDistance_;
			int64_t backDistance_;
			JunctionStorage::JunctionIterator front_;
			JunctionStorage::JunctionIterator back_;
		public:			
		
			Instance()
			{

			}

			Instance(JunctionStorage::JunctionIterator & it, int64_t distance) : front_(it), back_(it), frontDistance_(distance), backDistance_(distance)
			{

			}

			void ChangeFront(const JunctionStorage::JunctionIterator & it, int64_t distance)
			{
				front_ = it;
				frontDistance_ = distance;
			}

			void ChangeBack(const JunctionStorage::JunctionIterator & it, int64_t distance)
			{
				back_ = it;
				backDistance_ = distance;
			}			

			bool SinglePoint() const
			{
				return front_ == back_;
			}

			JunctionStorage::JunctionIterator Front() const
			{
				return front_;
			}

			JunctionStorage::JunctionIterator Back() const
			{
				return back_;
			}

			int64_t LeftFlankDistance() const
			{
				return frontDistance_;
			}

			int64_t RightFlankDistance() const
			{
				return backDistance_;
			}

			bool Within(const JunctionStorage::JunctionIterator it) const
			{
				if (it.GetChrId() == front_.GetChrId())
				{
					int64_t left = min(front_.GetIndex(), back_.GetIndex());
					int64_t right = max(front_.GetIndex(), back_.GetIndex());
					return it.GetIndex() >= left && it.GetIndex() <= right;
				}

				return false;
			}

			bool operator < (const Instance & inst) const
			{
				if (front_.GetChrId() != inst.front_.GetChrId())
				{
					return front_.GetChrId() < inst.front_.GetChrId();
				}

				int64_t idx1 = back_.IsPositiveStrand() ? back_.GetIndex() : front_.GetIndex();
				int64_t idx2 = inst.back_.IsPositiveStrand() ? inst.back_.GetIndex() : inst.front_.GetIndex();
				return idx1 < idx2;
			}
		};

		typedef std::multiset<Instance> InstanceSet;

		struct Point
		{
		private:
			Edge edge;
			int64_t startDistance;
		public:
			Point() {}
			Point(Edge edge, int64_t startDistance) : edge(edge), startDistance(startDistance) {}

			Edge GetEdge() const
			{
				return edge;
			}

			int64_t StartDistance() const
			{
				return startDistance;
			}

			int64_t EndDistance() const
			{
				return startDistance + edge.GetLength();
			}

			bool operator == (const Point & p) const
			{
				return startDistance == p.startDistance && edge == p.edge;
			}

			bool operator != (const Point & p) const
			{
				return p != *this;
			}
		};

		int64_t Origin() const
		{
			return origin_;
		}		

		const InstanceSet & Instances() const
		{
			return instance_;
		}

		int64_t LeftDistance() const
		{
			return -leftBodyFlank_;
		}

		int64_t RightDistance() const
		{
			return rightBodyFlank_;
		}

		int64_t MiddlePathLength() const
		{
			return LeftDistance() + RightDistance();
		}

		int64_t GetEndVertex() const
		{
			if (rightBody_.size() > 0)
			{
				return rightBody_.back().GetEdge().GetEndVertex();
			}

			return origin_;
		}

		int64_t GetStartVertex() const
		{
			if (leftBody_.size() > 0)
			{
				return leftBody_.back().GetEdge().GetStartVertex();
			}

			return origin_;
		}

		void DumpInstances(std::ostream & out) const
		{
			for (auto inst : instance_)
			{
				out << "(" << (inst.Front().IsPositiveStrand() ? '+' : '-') << inst.Front().GetChrId() << ' ' << inst.Front().GetIndex() << ' ' << inst.Back().GetIndex() << ')' << std::endl;
			}
		}

		void DumpPath(std::vector<Edge> & ret) const
		{
			ret.clear();
			for (auto it = leftBody_.rbegin(); it != leftBody_.rend(); ++it)
			{
				ret.push_back(it->GetEdge());
			}

			for (auto it = rightBody_.rbegin(); it != rightBody_.rend(); ++it)
			{
				ret.push_back(it->GetEdge());
			}
		}

		bool Compatible(const JunctionStorage::JunctionIterator & start, const JunctionStorage::JunctionIterator & end, const Edge & e) const
		{
			if (start.GetChrId() != end.GetChrId() || start.IsPositiveStrand() != end.IsPositiveStrand())
			{
				return false;
			}

			int64_t diff = end.GetPosition() - start.GetPosition();
			if (start.IsPositiveStrand())
			{
				if (diff < 0)
				{
					return false;
				}

				auto start1 = start + 1;
				if (diff > maxBranchSize_ && (start.GetChar() != e.GetChar() || end != start1 || start1.GetVertexId() != e.GetEndVertex()))
				{
					return false;
				}
			}
			else
			{
				if (-diff < 0)
				{
					return false;
				}

				auto start1 = start + 1;
				if (-diff > maxBranchSize_ && (start.GetChar() != e.GetChar() || end != start1 || start1.GetVertexId() != e.GetEndVertex()))
				{
					return false;
				}
			}

			return true;
		}		

		class PointPushFrontWorker
		{
		public:
			Edge e;
			Path * path;
			int64_t vertex;
			int64_t distance;
			bool & failFlag;

			PointPushFrontWorker(Path * path, int64_t vertex, int64_t distance, Edge e, bool & failFlag) : path(path), vertex(vertex), e(e), failFlag(failFlag), distance(distance)
			{

			}

			void operator()(const tbb::blocked_range<size_t> & range) const
			{
				for (size_t i = range.begin(); i != range.end() && !failFlag; i++)
				{
					bool newInstance = true;
					JunctionStorage::JunctionIterator nowIt = path->storage_->GetJunctionInstance(vertex, i);
					if (!nowIt.IsUsed())
					{
						auto inst = path->instance_.upper_bound(Instance(nowIt, 0));
						if (inst != path->instance_.end() && inst->Within(nowIt))
						{
							continue;
						}

						if (nowIt.IsPositiveStrand())
						{
							if (inst != path->instance_.end() && path->Compatible(nowIt, inst->Front(), e))
							{
								newInstance = false;
							}
						}
						else
						{
							if (inst != path->instance_.begin() && path->Compatible(nowIt, (--inst)->Front(), e))
							{
								newInstance = false;
							}
						}

						if (!newInstance && inst->Front().GetVertexId() != vertex)
						{
							int64_t nextLength = abs(nowIt.GetPosition() - inst->Back().GetPosition());
							int64_t rightFlankSize = path->rightBodyFlank_ - inst->RightFlankDistance();
							assert(rightFlankSize >= 0);
							if (nextLength >= path->minChainSize_ && rightFlankSize > path->maxFlankingSize_)
							{
								failFlag = true;
								break;
							}

							const_cast<Instance&>(*inst).ChangeFront(nowIt, distance);
						}
						else
						{
							path->instance_.insert(Instance(nowIt, distance));
						}
					}
				}
			}

		};

		class PointPushBackWorker
		{
		public:
			Edge e;
			Path * path;
			int64_t vertex;
			int64_t distance;
			bool & failFlag;

			PointPushBackWorker(Path * path, int64_t vertex, int64_t distance, Edge e, bool & failFlag) : path(path), vertex(vertex), e(e), failFlag(failFlag), distance(distance)
			{

			}

			void operator()(const tbb::blocked_range<size_t> & range) const
			{
				for (size_t i = range.begin(); i < range.end() && !failFlag; i++)
				{
					bool newInstance = true;
					JunctionStorage::JunctionIterator nowIt = path->storage_->GetJunctionInstance(vertex, i);
					if (!nowIt.IsUsed())
					{
						auto inst = path->instance_.upper_bound(Instance(nowIt, 0));
						if (inst != path->instance_.end() && inst->Within(nowIt))
						{
							continue;
						}

						if (nowIt.IsPositiveStrand())
						{
							if (inst != path->instance_.begin() && path->Compatible((--inst)->Back(), nowIt, e))
							{
								newInstance = false;
							}
						}
						else
						{
							if (inst != path->instance_.end() && path->Compatible(inst->Back(), nowIt, e))
							{
								newInstance = false;
							}
						}

						if (!newInstance && inst->Back().GetVertexId() != vertex)
						{
							int64_t nextLength = abs(nowIt.GetPosition() - inst->Front().GetPosition());
							int64_t leftFlankSize = -(path->leftBodyFlank_ - inst->LeftFlankDistance());
							assert(leftFlankSize >= 0);
							if (nextLength >= path->minChainSize_ && leftFlankSize > path->maxFlankingSize_)
							{
								failFlag = true;
								break;
							}

							const_cast<Instance&>(*inst).ChangeBack(nowIt, distance);
						}
						else
						{
							path->instance_.insert(Instance(nowIt, distance));
						}
					}
				}
			}

		};

		bool PointPushBack(const Edge & e)
		{
			int64_t vertex = e.GetEndVertex();
			if (distanceKeeper_.IsSet(vertex))
			{
				return false;
			}

			bool failFlag = false;
			int64_t startVertexDistance = rightBodyFlank_;
			int64_t endVertexDistance = startVertexDistance + e.GetLength();
			PointPushBackWorker(this, vertex, endVertexDistance, e, failFlag)(tbb::blocked_range<size_t>(0, storage_->GetInstancesCount(vertex)));
			rightBody_.push_back(Point(e, startVertexDistance));
			distanceKeeper_.Set(e.GetEndVertex(), endVertexDistance);
			rightBodyFlank_ = rightBody_.back().EndDistance();

			if (failFlag)
			{
				PointPopBack();
			}
			
			return !failFlag;
		}

		void PointPopBack()
		{
			int64_t lastVertex = rightBody_.back().GetEdge().GetEndVertex();
			rightBody_.pop_back();			
			distanceKeeper_.Unset(lastVertex);
			rightBodyFlank_ = rightBody_.empty() ? 0 : rightBody_.back().EndDistance();
			for (auto it = instance_.begin(); it != instance_.end(); )
			{
				if (it->Back().GetVertexId() == lastVertex)
				{
					if (it->Front() == it->Back())
					{
						it = instance_.erase(it);
					}
					else
					{
						auto jt = it->Back();
						while (true)
						{
							if (distanceKeeper_.IsSet(jt.GetVertexId()))
							{
								const_cast<Instance&>(*it).ChangeBack(jt, distanceKeeper_.Get(jt.GetVertexId()));
								break;
							}
							else
							{
								--jt;
							}
						}

						it++;
					}
				}
				else
				{
					++it;
				}
			}
		}

		bool PointPushFront(const Edge & e)
		{
			int64_t vertex = e.GetStartVertex();
			if (distanceKeeper_.IsSet(vertex))
			{
				return false;
			}

			bool failFlag = false;
			int64_t endVertexDistance = leftBodyFlank_;
			int64_t startVertexDistance = endVertexDistance - e.GetLength();
			PointPushFrontWorker(this, vertex, startVertexDistance, e, failFlag)(tbb::blocked_range<size_t>(0, storage_->GetInstancesCount(vertex)));
			leftBody_.push_back(Point(e, startVertexDistance));
			distanceKeeper_.Set(e.GetStartVertex(), startVertexDistance);
			leftBodyFlank_ = leftBody_.back().StartDistance();

			if (failFlag)
			{
				PointPopFront();
			}
		
			return !failFlag;
		}

		void PointPopFront()
		{
			int64_t lastVertex = leftBody_.back().GetEdge().GetStartVertex();
			leftBody_.pop_back();
			distanceKeeper_.Unset(lastVertex);
			leftBodyFlank_ = leftBody_.empty() ? 0 : leftBody_.back().StartDistance();
			for (auto it = instance_.begin(); it != instance_.end(); )
			{
				if (it->Front().GetVertexId() == lastVertex)
				{	
					if (it->Front() == it->Back())
					{
						it = instance_.erase(it);
					}
					else
					{
						auto jt = it->Front();
						while (true)
						{
							if (distanceKeeper_.IsSet(jt.GetVertexId()))
							{
								const_cast<Instance&>(*it).ChangeFront(jt, distanceKeeper_.Get(jt.GetVertexId()));
								break;
							}
							else
							{
								++jt;
							}
						}

						it++;
					}
				}
				else
				{
					++it;
				}
			}
		}

		int64_t Score(bool final = false) const
		{
			int64_t score;
			int64_t length;
			int64_t ret = 0;
			int64_t middlePath = MiddlePathLength();
			for (auto & inst : instance_)
			{
				InstanceScore(inst, length, score, middlePath);
				if (!final || length >= minChainSize_)
				{
					ret += score;
				}
			}

			return ret;
		}

		int64_t GoodInstances() const
		{
			int64_t ret = 0;
			for (auto & it : instance_)
			{
				if (IsGoodInstance(it))
				{
					ret++;
				}
			}

			return ret;
		}

		bool IsGoodInstance(const Instance & it) const
		{
			int64_t score;
			int64_t length;
			InstanceScore(it, length, score, MiddlePathLength());
			return length >= minChainSize_;
		}

		void InstanceScore(const Instance & inst, int64_t & length, int64_t & score, int64_t middlePath) const
		{			
			length = abs(inst.Front().GetPosition() - inst.Back().GetPosition());
			score = length - (middlePath - length);
		}

		void Clear()
		{
			for (auto pt : leftBody_)
			{
				distanceKeeper_.Unset(pt.GetEdge().GetStartVertex());
			}

			for (auto pt : rightBody_)
			{
				distanceKeeper_.Unset(pt.GetEdge().GetEndVertex());
			}

			leftBody_.clear();
			rightBody_.clear();
			instance_.clear();
			distanceKeeper_.Unset(origin_);			
			for (int64_t v1 = -storage_->GetVerticesNumber() + 1; v1 < storage_->GetVerticesNumber(); v1++)
			{
				assert(!distanceKeeper_.IsSet(v1));
			}

			instance_.clear();
		}

	private:


		friend struct BestPath;

		std::vector<Point> leftBody_;
		std::vector<Point> rightBody_;
		InstanceSet instance_;

		int64_t origin_;
		int64_t minChainSize_;
		int64_t minBlockSize_;
		int64_t maxBranchSize_;
		int64_t maxFlankingSize_;
		int64_t leftBodyFlank_;
		int64_t rightBodyFlank_;
		DistanceKeeper distanceKeeper_;
		const JunctionStorage * storage_;
	};

	struct BestPath
	{
		int64_t score_;
		int64_t leftFlank_;
		int64_t rightFlank_;
		std::vector<Path::Point> newLeftBody_;
		std::vector<Path::Point> newRightBody_;

		void FixForward(Path & path)
		{
			for (auto & pt : newRightBody_)
			{
				bool ret = path.PointPushBack(pt.GetEdge());
				assert(ret);
			}

			newRightBody_.clear();
			rightFlank_ = path.rightBody_.size();
		}

		void FixBackward(Path & path)
		{
			for (auto & pt : newLeftBody_)
			{
				bool ret = path.PointPushFront(pt.GetEdge());
				assert(ret);
			}

			newLeftBody_.clear();
			leftFlank_ = path.leftBody_.size();
		}

		void UpdateForward(const Path & path, int64_t newScore)
		{
			score_ = newScore;
			newRightBody_.clear();
			std::copy(path.rightBody_.begin() + rightFlank_, path.rightBody_.end(), std::back_inserter(newRightBody_));
		}

		void UpdateBackward(const Path & path, int64_t newScore)
		{
			score_ = newScore;
			newLeftBody_.clear();
			std::copy(path.leftBody_.begin() + leftFlank_, path.leftBody_.end(), std::back_inserter(newLeftBody_));
		}

		BestPath() 
		{
			Init();
		}

		void Init()
		{
			score_ = leftFlank_ = rightFlank_ = 0;
		}
	};
}

#endif

