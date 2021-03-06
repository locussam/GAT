#pragma once
class MBB
{
public:
	float xmin, ymin, xmax, ymax;
	MBB(float val_xmin, float val_ymin, float val_xmax, float val_ymax);
	bool pInBox(float x, float y);
	int intersect(MBB& b);
	int randomGenerateMBB(MBB &generated);
	MBB();
	~MBB();
};

