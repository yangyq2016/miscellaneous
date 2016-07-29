#pragma once
// ¿é×ø±êÏµ
/*	        90
 *	  		 |
 *	         |   
 *	         |
 *-180-------0-------180
 *           |
 *           |	
 *           |
 *          -90
 */ 


class QuadID
{
public:
	int	m_row_num;	//row index
	int	m_col_num;	//column index

	// constructor
	QuadID() : m_row_num(0), m_col_num(0){}
	QuadID(int row_num,int col_num) : m_row_num(row_num), m_col_num(col_num){}

	// copy constructor
	QuadID(const QuadID& rhs) : m_row_num(rhs.m_row_num), m_col_num(rhs.m_col_num){}

	// assign operator
	QuadID& operator = (const QuadID& rhs)
	{
		if(this == &rhs)
			return *this;
		m_row_num = rhs.m_row_num;
		m_col_num = rhs.m_col_num;
		return *this;
	}

	// == operator
	bool operator == (const QuadID& rhs) const
	{
		return !memcmp(this, &rhs, sizeof(QuadID));
	}

	// < operator
	bool operator < (const QuadID& rhs) const
	{
		return memcmp(this, &rhs, sizeof(QuadID)) < 0;
	}
};
