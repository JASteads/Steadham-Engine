#if !defined(GRAPHICS_H)

#define PI 3.14159265359f

local float Abs(float num)
{
	if (num < 0) return -num;
	return num;
}
local float Pow(float num, float power)
{
	for (int x = 1; x < Abs(power); x++)
		num *= num;
	if (power < 0)
		return 1.0f / num;
	return num;
}
local float Sqrt(float num) 
{
	
}

inline int Round_float(float val)
{
	return (int)(val + 0.5f);
}

struct Point
{
	float x, y;
	inline Point &operator+= (Point b);
	inline Point &operator*= (float b);
	inline Point &operator- ();
};
inline Point operator+ (Point a, Point b)
{
	return { a.x + b.x, a.y + b.y };
}
inline Point operator+ (Point a, float b)
{
	return { a.x + b, a.y + b };
}
inline Point operator- (Point a, Point b)
{
	return { a.x - b.x, a.y - b.y };
}
inline Point operator* (Point a, float b)
{
	return { a.x * b, a.y * b };
}
inline Point &Point::operator*= (float b)
{
	*this = *this * b;
	return *this;
}
inline Point &Point::operator+= (Point b)
{
	*this = b + *this;
	return *this;
}
inline Point &Point::operator- ()
{
	*this = { -this->x, -this->y };
	return *this;
}
inline bool operator== (Point a, Point b)
{
	if (a.x == b.x && a.y == b.y) return true;
	else return false;
}

struct Color
{
	float r, g, b;
}
black = { 0, 0, 0 },
white = { 1, 1, 1 },
blue = { 0, 0, 1 },
green = { 0, 1, 0 },
red = { 1, 0, 0 },
teal = { 0 , 1 , 1 },
violet = { 1, 0, 1 },
yellow = { 1, 1, 0 };
struct Rect
{
	Point p1, p2;
	float w = p2.x - p1.x, h = p2.y - p1.y;
};
struct Box
{
	Rect area;
	Color color;
	bool hovered;
};
#define GRAPHICS_H
#endif