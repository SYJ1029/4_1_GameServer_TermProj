#pragma once
#include "globals.h"

class SECTOR {
public:
	std::unordered_set<int> m_objects;
	std::mutex m_mutex;

	void add_object(int object_id)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_objects.insert(object_id);
	}

	void remove_object(int object_id)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_objects.erase(object_id);
	}
};

class SectorManager {
public:
	SectorManager()
	{
		for (auto& sector : sectors)
			sector = std::make_shared<SECTOR>();
	}

	void add_object_to_sector(int object_id, short x, short y)
	{
		sectors[sector_id(x, y)]->add_object(object_id);
	}

	void remove_object_from_sector(int object_id, short x, short y)
	{
		sectors[sector_id(x, y)]->remove_object(object_id);
	}

	void update_object_sector(int object_id, short old_x, short old_y, short new_x, short new_y)
	{
		int old_sid = sector_id(old_x, old_y);
		int new_sid = sector_id(new_x, new_y);
		if (old_sid == new_sid) return;

		sectors[old_sid]->remove_object(object_id);
		sectors[new_sid]->add_object(object_id);
	}

	std::vector<int> get_objects_in_adjacent_sectors(short x, short y)
	{
		std::vector<int> result;
		int sx = SECTOR_X(x);
		int sy = SECTOR_Y(y);
		for (int dy = -1; dy <= 1; ++dy) {
			for (int dx = -1; dx <= 1; ++dx) {
				int nsx = sx + dx;
				int nsy = sy + dy;
				if (nsx < 0 || nsx >= MAX_SECTORS_X || nsy < 0 || nsy >= MAX_SECTORS_Y)
					continue;

				auto& sector = sectors[SECTOR_ID(nsx, nsy)];
				std::lock_guard<std::mutex> lock(sector->m_mutex);
				result.insert(result.end(), sector->m_objects.begin(), sector->m_objects.end());
			}
		}
		return result;
	}

private:
	int sector_id(short x, short y)
	{
		return SECTOR_ID(SECTOR_X(x), SECTOR_Y(y));
	}

	std::array<std::shared_ptr<SECTOR>, MAX_SECTORS_X* MAX_SECTORS_Y> sectors;
};
