#include "Grid.h"
#include "WinTimer.h"

extern Trajectory* tradb;
extern void *baseAddrGPU;
MyTimer timer;

Grid::Grid()
{
	range = MBB(0, 0, 0, 0);
	cellnum = 0;
	cell_size = 0;
	cellNum_axis = 0;
	cellPtr = NULL;
	QuadtreeNode *root;
	allPoints = NULL;
	allPointsPtrGPU = NULL;

}

//测试过，没问题
int getIdxFromXY(int x, int y)
{
	int lenx, leny;
	if (x == 0)
		lenx = 1;
	else
	{
		lenx = int(log2(x)) + 1;
	}
	if (y == 0)
		leny = 1;
	else
		leny = int(log2(y)) + 1;
	int result = 0;
	int xbit = 1, ybit = 1;
	for (int i = 1; i <= 2 * max(lenx, leny); i++) {
		if ((i & 1) == 1) //奇数
		{
			result += (x >> (xbit - 1) & 1) * (1 << (i - 1));
			xbit = xbit + 1;
		}
		else //偶数
		{
			result += (y >> (ybit - 1) & 1) * (1 << (i - 1));
			ybit = ybit + 1;
		}
	}
	return result;
}

int Grid::buildQuadTree(int level, int id, QuadtreeNode* pNode, QuadtreeNode *parent)
{
	int totalLevel = int(log2(this->cellnum) / log2(4));
	int totalPoints = 0;
	for (int i = id*int(pow(4, (totalLevel - level))); i <= (id + 1) * int(pow(4, (totalLevel - level))) - 1; i++) {
		totalPoints += this->cellPtr[i].totalPointNum;
	}
	pNode->mbb = MBB(this->cellPtr[id*int(pow(4, (totalLevel - level)))].mbb.xmin, this->cellPtr[(id + 1) * int(pow(4, (totalLevel - level))) - 1].mbb.ymin, this->cellPtr[(id + 1) * int(pow(4, (totalLevel - level))) - 1].mbb.xmax, this->cellPtr[id*int(pow(4, (totalLevel - level)))].mbb.ymax);
	pNode->numPoints = totalPoints;
	pNode->NodeID = id;
	pNode->parent = parent;
	pNode->level = level;
	if ((totalPoints < MAXPOINTINNODE)||(level==totalLevel)) {
		pNode->isLeaf = true;
		pNode->DL = NULL;
		pNode->DR = NULL;
		pNode->UL = NULL;
		pNode->UR = NULL;
		return 0;
	}
	else {
		pNode->isLeaf = false;
		pNode->UL = (QuadtreeNode*)malloc(sizeof(QuadtreeNode));
		this->buildQuadTree(level + 1, id << 2, pNode->UL, pNode);
		pNode->UR = (QuadtreeNode*)malloc(sizeof(QuadtreeNode));
		this->buildQuadTree(level + 1, (id << 2)+1, pNode->UR, pNode);
		pNode->DL = (QuadtreeNode*)malloc(sizeof(QuadtreeNode));
		this->buildQuadTree(level + 1, (id << 2)+2, pNode->DL, pNode);
		pNode->DR = (QuadtreeNode*)malloc(sizeof(QuadtreeNode));
		this->buildQuadTree(level + 1, (id << 2)+3, pNode->DR, pNode);
		return 0;
	}

}



Grid::Grid(const MBB& mbb,float val_cell_size)
{
	range = mbb;
	cell_size = val_cell_size;
	//貌似只需要用一个维度就行了，因为规定好了必须是2*2,4*4，……
	int divideNumOnX = (int)((mbb.xmax - mbb.xmin) / val_cell_size) + 1; //最少要用多少个cell
	int divideNumOnY = (int)((mbb.ymax - mbb.ymin) / val_cell_size) + 1;
	int maxValue = max(divideNumOnX, divideNumOnY);
	//找到最佳的长宽
	cellNum_axis = maxValue >> (int(log2(maxValue))) << (int(log2(maxValue)) + 1);
	cellnum = cellNum_axis * cellNum_axis;
	cellPtr = new Cell[cellnum];
	//由于满足正方形需要，向xmax、ymin方向扩展range

	//注意cell编号是从(xmin,ymax)开始的，而不是(xmin,ymin)
	//Z字形编码
	for (int i = 0; i <= cellNum_axis - 1; i++) {
		for (int j = 0; j <= cellNum_axis - 1; j++) {
			int cell_idx = getIdxFromXY(j, i);
			cellPtr[cell_idx].initial(i, j, MBB(range.xmin + cell_size*j, range.ymax - cell_size*(i+1), range.xmin + cell_size*(j + 1), range.ymax - cell_size*(i)));
		}
	}
}

//把轨迹t打碎成子轨迹，添加到cell里面
//这一步仅仅是把子轨迹放进了cell里面，组成一个个item
int Grid::addTrajectoryIntoCell(Trajectory &t)
{
	if (t.length == 0)
		return 1;//空轨迹
	SamplePoint p = t.points[0];
	int lastCellNo = WhichCellPointIn(p); 
	int lastCellStartIdx = 0;
	int nowCellNo;
	//cell based traj生成，记得转换后free！
	vector<int> *tempCellBasedTraj = new vector<int>;
	tempCellBasedTraj->reserve(1048577);
	int tempCellNum = 0;
	for (int i = 1; i <= t.length - 1; i++) {
		p = t.points[i];
		nowCellNo = WhichCellPointIn(p);
		if (i == t.length - 1)
		{
			//到最后一条，发现这个cell也是上个cell就是最后一个cell了，添加之
			if (lastCellNo == nowCellNo)
			{
				tempCellNum++;
				tempCellBasedTraj->push_back(nowCellNo);
				cellPtr[nowCellNo].addSubTra(t.tid, lastCellStartIdx, i, i - lastCellStartIdx + 1);
			}
			//否则，上一个和这个cell都要添加
			else
			{
				tempCellNum += 2;
				tempCellBasedTraj->push_back(lastCellNo);
				tempCellBasedTraj->push_back(nowCellNo);
				cellPtr[lastCellNo].addSubTra(t.tid, lastCellStartIdx, i - 1, i - 1 - lastCellStartIdx + 1);
				cellPtr[nowCellNo].addSubTra(t.tid, i, i, 1);
			}
		}
		else
		{
			if (lastCellNo == nowCellNo)
				continue;
			else
			{
				// 终结一条子轨迹，开始下一条子轨迹
				//cellTra里面加一条
				tempCellNum++;
				tempCellBasedTraj->push_back(lastCellNo);
				//SubTra添加
				cellPtr[lastCellNo].addSubTra(t.tid, lastCellStartIdx, i - 1, i - 1 - lastCellStartIdx + 1);
				lastCellNo = nowCellNo;
				lastCellStartIdx = i;
			}
		}
	}
	this->cellBasedTrajectory[t.tid].length = tempCellNum;
	this->cellBasedTrajectory[t.tid].cellNo = (int*)malloc(sizeof(int)*tempCellNum);
	if (this->cellBasedTrajectory[t.tid].cellNo == NULL) throw("alloc error");
	for (int i = 0; i <= tempCellNum - 1; i++) {
		this->cellBasedTrajectory[t.tid].cellNo[i] = tempCellBasedTraj->at(i);
	}
	delete tempCellBasedTraj;
	return 0;
}

//确认无误
int Grid::WhichCellPointIn(SamplePoint p)
{
	//注意cell编号是从(xmin,ymax)开始的，而不是(xmin,ymin)
	int row = (int)((range.ymax - p.lat) / cell_size); //从0开始
	int col = (int)((p.lon - range.xmin) / cell_size); //从0开始
	return getIdxFromXY(col, row);
}

int Grid::addDatasetToGrid(Trajectory * db, int traNum)
{
	this->trajNum = traNum;
	//注意，轨迹编号从1开始
	this->cellBasedTrajectory.resize(traNum + 1); //扩大cellbasedtraj的规模，加轨迹的时候可以直接用
	int pointCount = 0;
	for (int i = 1; i <= traNum; i++) {
		addTrajectoryIntoCell(db[i]);
	}
	for (int i = 0; i <= cellnum - 1; i++) {
		cellPtr[i].buildSubTraTable();
		pointCount += cellPtr[i].totalPointNum;
	}
	this->totalPointNum = pointCount;
	//链表变成了数组
	//subTraTable仅仅是记录了子轨迹（起始offset、终止offset、Tid）

	//建立Quadtree，自顶向下建立，分割节点使所有节点包含点的个数小于MAXPOINTINNODE
	this->root = (QuadtreeNode*)malloc(sizeof(QuadtreeNode));
	this->buildQuadTree(0, 0, this->root, NULL);

	//转化为cell连续存储
	//此处连续存储是指同一cell内的采样点存储在一起，有利于rangeQuery，但不利于similarity query
	//similarity组装轨迹的时候，可以先记录当前是第几个subtra，找轨迹的时候从这个往后找，避免tid重复存在的问题
	this->allPoints = (SPoint*)malloc(sizeof(SPoint)*(this->totalPointNum));
	pointCount = 0;
	for (int i = 0; i <= cellnum - 1; i++) {
		cellPtr[i].pointRangeStart = pointCount;
		for (int j = 0; j <= cellPtr[i].subTraNum - 1; j++) {
			for (int k = cellPtr[i].subTraTable[j].startpID; k <= cellPtr[i].subTraTable[j].endpID; k++) {
				allPoints[pointCount].tID = cellPtr[i].subTraTable[j].traID;
				allPoints[pointCount].x = tradb[allPoints[pointCount].tID].points[k].lon;
				allPoints[pointCount].y = tradb[allPoints[pointCount].tID].points[k].lat;
				//allPoints[pointCount].time = tradb[allPoints[pointCount].tID].points[k].time;
				pointCount++;
			}
		}
		cellPtr[i].pointRangeEnd = pointCount - 1;
		if (cellPtr[i].pointRangeEnd - cellPtr[i].pointRangeStart + 1 != cellPtr[i].totalPointNum)
			cerr << "Grid.cpp: something wrong in total point statistic" << endl;
	}
	//Delta Encoding的cell连续存储
	this->allPointsDeltaEncoding = (DPoint*)malloc(sizeof(DPoint)*(this->totalPointNum));
	pointCount = 0;
	for (int i = 0; i <= cellnum - 1; i++) {
		cellPtr[i].pointRangeStart = pointCount;
		for (int j = 0; j <= cellPtr[i].subTraNum - 1; j++) {
			for (int k = cellPtr[i].subTraTable[j].startpID; k <= cellPtr[i].subTraTable[j].endpID; k++) {
				allPointsDeltaEncoding[pointCount].tID = cellPtr[i].subTraTable[j].traID;
				allPointsDeltaEncoding[pointCount].x = short(int((tradb[allPointsDeltaEncoding[pointCount].tID].points[k].lon)*1000000)-cellPtr[i].anchorPointX);
				allPointsDeltaEncoding[pointCount].y = short(int((tradb[allPointsDeltaEncoding[pointCount].tID].points[k].lat)*1000000)-cellPtr[i].anchorPointY);
				pointCount++;
			}
		}
		cellPtr[i].pointRangeEnd = pointCount - 1;
		if (cellPtr[i].pointRangeEnd - cellPtr[i].pointRangeStart + 1 != cellPtr[i].totalPointNum)
			cerr << "Grid.cpp: something wrong in total point statistic" << endl;
	}

	//把生成好的allpoints放到GPU内
	//putCellDataSetIntoGPU(this->allPoints, this->allPointsPtrGPU, this->totalPointNum);


	return 0;
}

int Grid::writeCellsToFile(int * cellNo,int cellNum, string file)
// under editing....
{
	fout.open(file, ios_base::out);
	for (int i = 0; i <= cellNum - 1; i++) {
		int outCellIdx = cellNo[i];
		cout << outCellIdx << ": " << "[" << cellPtr[outCellIdx].mbb.xmin << "," <<cellPtr[outCellIdx].mbb.xmax << "," << cellPtr[outCellIdx].mbb.ymin << "," << cellPtr[outCellIdx].mbb.ymax << "]" << endl;
		for (int j = 0; j <= cellPtr[outCellIdx].subTraNum - 1; j++) {
			int tid = cellPtr[outCellIdx].subTraTable[j].traID;
			int startpid = cellPtr[outCellIdx].subTraTable[j].startpID;
			int endpid = cellPtr[outCellIdx].subTraTable[j].endpID;
			for (int k = startpid; k <= endpid; k++) {
				cout << tradb[tid].points[k].lat << "," << tradb[tid].points[k].lon << ";";
			}
			cout << endl;
		}
	}
	return 0;
}

int Grid::rangeQueryBatch(MBB * bounds, int rangeNum, CPURangeQueryResult * ResultTable, int * resultSetSize)
{
	ofstream out("queryResult.txt", ios::out);
	ResultTable = (CPURangeQueryResult*)malloc(sizeof(CPURangeQueryResult));
	ResultTable->traid = -1; //table开头traid为-1 flag
	ResultTable->next = NULL;
	resultSetSize = (int*)malloc(sizeof(int)*rangeNum);
	CPURangeQueryResult* newResult, *nowResult;
	nowResult = ResultTable;
	int totalLevel = int(log2(this->cellnum) / log2(4));
	for (int i = 0; i <= rangeNum - 1; i++) {
		//int candidateNodeNum = 0;
		resultSetSize[i] = 0;
		vector<QuadtreeNode*> cells;
		findMatchNodeInQuadTree(this->root, bounds[i], &cells);
		//printf("%d", cells.size());
		for (vector<QuadtreeNode*>::iterator iterV = cells.begin(); iterV != cells.end(); iterV++) {

			int nodeID = (*iterV)->NodeID;
			int nodeLevel = (*iterV)->level;
			int firstCellID = nodeID*int(pow(4, (totalLevel - nodeLevel)));
			int lastCellID = (nodeID + 1) * int(pow(4, (totalLevel - nodeLevel))) - 1;
			for (int cellID = firstCellID; cellID <= lastCellID; cellID++) {
				int anchorX = this->cellPtr[cellID].anchorPointX;
				int anchorY = this->cellPtr[cellID].anchorPointY;
				for (int idx = this->cellPtr[cellID].pointRangeStart; idx <= this->cellPtr[cellID].pointRangeEnd; idx++) {
					//compress
					//float realX = float(allPointsDeltaEncoding[idx].x + anchorX) / 1000000;
					//float realY = float(allPointsDeltaEncoding[idx].y + anchorY) / 1000000;
					// no compress
					float realX = allPoints[idx].x;
					float realY = allPoints[idx].y;
					if (bounds[i].pInBox(realX, realY))
					{
						//printf("%f,%f", realX, realY);
						//printf("%d\n", idx);
						//newResult = (CPURangeQueryResult*)malloc(sizeof(CPURangeQueryResult));
						//if (newResult == NULL)
						//	return 2; //分配内存失败
						//compress
						//newResult->traid = allPointsDeltaEncoding[idx].tID;
						//no compress
						//newResult->traid = allPoints[idx].tID;
						//newResult->x = realX;
						//newResult->y = realY;
						//// out << "Qid:" << i << "......." << newResult->x << "," << newResult->y << endl;
						//newResult->next = NULL;
						//nowResult->next = newResult;
						//nowResult = newResult;
						resultSetSize[i]++;

					}
				}
			}

		}
	}
	out.close();
	return 0;
}

int Grid::findMatchNodeInQuadTree(QuadtreeNode *node, MBB& bound, vector<QuadtreeNode*> *cells)
{
	if (node->isLeaf) {
		cells->push_back(node);
	}
	else
	{
		if (bound.intersect(node->UL->mbb))
			findMatchNodeInQuadTree(node->UL, bound, cells);
		if (bound.intersect(node->UR->mbb))
			findMatchNodeInQuadTree(node->UR, bound, cells);
		if (bound.intersect(node->DL->mbb))
			findMatchNodeInQuadTree(node->DL, bound, cells);
		if (bound.intersect(node->DR->mbb))
			findMatchNodeInQuadTree(node->DR, bound, cells);
	}
	return 0;
}



int Grid::rangeQueryBatchGPU(MBB * bounds, int rangeNum, CPURangeQueryResult * ResultTable, int * resultSetSize)
{
	// 分配GPU内存
	//MyTimer timer;
	// 参数随便设置的，可以再调
	//timer.start();

	RangeQueryStateTable *stateTableAllocate = (RangeQueryStateTable*)malloc(sizeof(RangeQueryStateTable) * 1000000);
	this->stateTableRange = stateTableAllocate;
	this->stateTableLength = 0;
	this->nodeAddrTableLength = 0;
	// for each query, generate the nodes:
	cudaStream_t stream;
	cudaStreamCreate(&stream);
	for (int i = 0; i <= rangeNum - 1; i++) {
		findMatchNodeInQuadTreeGPU(root, bounds[i], NULL, stream, i);
	}
	//stateTable中点的数目的最大值
	int maxPointNum = 0;
	for (int i = 0; i <= stateTableLength - 1; i++) {
		if (stateTableAllocate[i].candidatePointNum > maxPointNum)
			maxPointNum = stateTableAllocate[i].candidatePointNum;
	}
	//交给GPU进行并行查询
	//先传递stateTable
	//timer.stop();
	//cout << "Time 1:" << timer.elapse() << "ms" << endl;

	//timer.start();
	RangeQueryStateTable* stateTableGPU = NULL;
	CUDA_CALL(cudaMalloc((void**)&stateTableGPU, sizeof(RangeQueryStateTable)*this->stateTableLength));
	CUDA_CALL(cudaMemcpyAsync(stateTableGPU, stateTableAllocate, sizeof(RangeQueryStateTable)*this->stateTableLength,
		cudaMemcpyHostToDevice, stream));
	//传递完成，开始调用kernel查询
	uint8_t* resultsReturned = (uint8_t*)malloc(sizeof(uint8_t)*(this->trajNum+1)*rangeNum);

	//timer.stop();
	//cout << "Time 2:" << timer.elapse() << "ms" << endl;

	//timer.start();
	cudaRangeQueryTestHandler(stateTableGPU, stateTableLength, resultsReturned,this->trajNum+1,rangeNum, stream);
	//for (int jobID = 0; jobID <= rangeNum - 1; jobID++) {
	//	for (int traID = 0; traID <= this->trajNum; traID++) {
	//		if (resultsReturned[jobID*(this->trajNum + 1) + traID] == 1) {
	//			cout << "job " << jobID << "find" << traID << endl;
	//		}
	//	}
	//}
	//for (vector<uint8_t>::iterator iter = resultsReturned.begin(); iter != resultsReturned.end(); iter++) {
	//	//cout << (*iter) << endl;
	//	//printf("%d\n", *iter);
	//}
	//timer.stop();
	//cout << "Time 3:" << timer.elapse() << "ms" << endl;

	//FILE *fp = fopen("resultQuery.txt", "w+");
	//for (int i = 0; i <= stateTableLength - 1; i++) {
	//	for (int j = 0; j <= stateTableAllocate[i].candidatePointNum - 1; j++) {

	//		if ((resultsReturned[i*maxPointNum + j]) == (uint8_t)(1)) {
	//			fprintf(fp,"%d\n", stateTableAllocate[i].startIdxInAllPoints + j);
	//			fprintf(fp,"%f,%f\n", allPoints[stateTableAllocate[i].startIdxInAllPoints + j].x, allPoints[stateTableAllocate[i].startIdxInAllPoints + j].y);
	//		}

	//	}
	//}
		//查询结束，善后，清空stateTable，清空gpu等
		this->stateTableRange = stateTableAllocate;
		return 0;
}

int Grid::findMatchNodeInQuadTreeGPU(QuadtreeNode *node, MBB& bound, vector<QuadtreeNode*> *cells, cudaStream_t stream, int queryID)
{
	int totalLevel = int(log2(this->cellnum) / log2(4));
	if (node->isLeaf) {
		int startCellID = node->NodeID*int(pow(4, (totalLevel - node->level)));
		int startIdx = this->cellPtr[startCellID].pointRangeStart;
		int pointNum = node->numPoints;
		//如果gpu内存中没有该node的信息
		if (this->nodeAddrTable.find(startCellID) == this->nodeAddrTable.end()) {
			CUDA_CALL(cudaMemcpyAsync(baseAddrGPU, &(this->allPoints[startIdx]), pointNum*sizeof(SPoint), cudaMemcpyHostToDevice, stream));
			this->stateTableRange->ptr = baseAddrGPU;
			this->nodeAddrTable.insert(pair<int, void*>(startCellID, baseAddrGPU));
			baseAddrGPU = (void*)((char*)baseAddrGPU + pointNum*sizeof(SPoint));
		}
		//如果有，不再复制，直接用
		else {
			this->stateTableRange->ptr = this->nodeAddrTable.find(startCellID)->second;
		}
		
		this->stateTableRange->xmin = bound.xmin;
		this->stateTableRange->xmax = bound.xmax;
		this->stateTableRange->ymin = bound.ymin;
		this->stateTableRange->ymax = bound.ymax;
		this->stateTableRange->candidatePointNum = pointNum;
		this->stateTableRange->startIdxInAllPoints = startIdx;
		this->stateTableRange->queryID = queryID;
		this->stateTableRange = this->stateTableRange + 1;
		this->stateTableLength = this->stateTableLength + 1;
	}
	else
	{
		if (bound.intersect(node->UL->mbb))
			findMatchNodeInQuadTreeGPU(node->UL, bound, cells, stream, queryID);
		if (bound.intersect(node->UR->mbb))
			findMatchNodeInQuadTreeGPU(node->UR, bound, cells, stream, queryID);
		if (bound.intersect(node->DL->mbb))
			findMatchNodeInQuadTreeGPU(node->DL, bound, cells, stream, queryID);
		if (bound.intersect(node->DR->mbb))
			findMatchNodeInQuadTreeGPU(node->DR, bound, cells, stream, queryID);
	}
	return 0;
}

int Grid::SimilarityQueryBatch(Trajectory * qTra, int queryTrajNum, int * EDRdistance)
{
	return 0;
}



int Grid::SimilarityQuery(Trajectory & qTra, Trajectory **candTra, const int candSize, float * EDRdistance)
{
	cout << candSize << endl;
	SPoint *queryTra = (SPoint*)malloc(sizeof(SPoint)*(qTra.length));
	for (int i = 0; i <= qTra.length - 1; i++) {
		queryTra[i].x = qTra.points[i].lon;
		queryTra[i].y = qTra.points[i].lat;
		queryTra[i].tID = qTra.points[i].tid;
	}

	SPoint **candidateTra = (SPoint**)malloc(sizeof(SPoint*)*candSize);

	for (int i = 0; i <= candSize - 1; i++) {
		candidateTra[i] = (SPoint*)malloc(sizeof(SPoint)*(candTra[i]->length)); //调试的时候这一部分总报内存错误，FFFFF
		for (int j = 0; j <= candTra[i]->length - 1; j++) {
			candidateTra[i][j].x = candTra[i]->points[j].lon;
			candidateTra[i][j].y = candTra[i]->points[j].lat;
			candidateTra[i][j].tID = candTra[i]->points[j].tid;
		}
	}

	int queryLength=qTra.length;
	int *candidateLength = (int*)malloc(sizeof(int)*candSize);
	for (int i = 0; i <= candSize - 1; i++) {
		candidateLength[i] = candTra[i]->length;
	}

	int* result = (int*)malloc(sizeof(int)*candSize);

	MyTimer timer1;
	timer1.start();

	//CPU
	int *resultCPU = (int*)malloc(sizeof(int)*candSize);
	for (int i = 0; i <= candSize - 1; i++) {
		//每个DP问题
		SPoint *CPUqueryTra = queryTra,*CPUCandTra = candidateTra[i];
		int CPUqueryLength = qTra.length, CPUCandLength = candidateLength[i];
		int longest=0;

		const SPoint *tra1, *tra2;
		int len1, len2;
		if (CPUCandLength >= CPUqueryLength) {
			tra1 = CPUqueryTra;
			tra2 = CPUCandTra;
			len1 = CPUqueryLength;
			len2 = CPUCandLength;
		}
		else
		{
			tra1 = CPUCandTra;
			tra2 = CPUqueryTra;
			len1 = CPUCandLength;
			len2 = CPUqueryLength;
		}

		if (CPUqueryLength >= longest) {
			longest = CPUqueryLength;
		}
		else
		{
			longest = CPUCandLength;
		}


		int **stateTable = (int**)malloc(sizeof(int*)*(len1 + 1));
		for (int j = 0; j <= len1; j++) {
			stateTable[j] = (int*)malloc(sizeof(int)*(len2 + 1));
		}
		stateTable[0][0] = 0;
		for (int row = 1; row <= len1; row++) {
			stateTable[row][0] = row;
		}
		for (int col = 1; col <= len2; col++) {
			stateTable[0][col] = col;
		}

		for (int row = 1; row <= len1; row++) {
			for (int col = 1; col <= len2; col++) {
				SPoint p1 = tra1[row-1];
				SPoint p2 = tra2[col-1]; //这样做内存是聚集访问的吗？
				bool subcost;
				if (((p1.x - p2.x)*(p1.x - p2.x) + (p1.y - p2.y)*(p1.y - p2.y)) < EPSILON) {
					subcost = 0;
				}
				else
					subcost = 1;
				int myState = 0;
				int state_ismatch = stateTable[row-1][col-1] + subcost;
				int state_up = stateTable[row-1][col] + 1;
				int state_left = stateTable[row][col-1] + 1;
				if (state_ismatch < state_up)
					myState = state_ismatch;
				else if (state_left < state_up)
					myState = state_left;
				else
					myState = state_ismatch;

				stateTable[row][col] = myState;
			//	if (row == len1&&col == len2)
					//cout << myState << endl;
			}
		}
		
		resultCPU[i] = stateTable[len1][len2];
		//cout << resultCPU[i] << endl;
	}
	timer1.stop();
	cout << "CPU Similarity Time:" << timer1.elapse() << "ms" << endl;
	//GPU

	timer1.start();
	handleEDRdistance(queryTra, candidateTra, candSize, queryLength, candidateLength, result);
	timer1.stop();
	cout << "GPU Similarity Time:" << timer1.elapse() << "ms" << endl;

	for (int i = 0; i <= candSize - 1; i++) {
		EDRdistance[i] = result[i];
	}
	free(queryTra);
	for (int i = 0; i <= candSize - 1; i++) {
		free(candidateTra[i]);
	}
	free(candidateTra);
	free(candidateLength);
	free(result);

	return 0;
}



Grid::~Grid()
{
}
