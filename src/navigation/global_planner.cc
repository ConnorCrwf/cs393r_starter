#include "global_planner.h"

using std::string;
using std::vector;
using Eigen::Vector2f;
using Eigen::Vector2i;
using std::cout;
using std::endl;
using geometry::line2f;

//========================= GENERAL FUNCTIONS =========================//

GlobalPlanner::GlobalPlanner(){
	// Initialize blueprint map
	map_.Load("maps/GDC1.txt");
	cout << "Initialized GDC1 map with " << map_.lines.size() << " lines." << endl;
}

void GlobalPlanner::setResolution(float resolution){
	map_resolution_ = resolution;
	cout << "Resolution set to: " << map_resolution_ << endl;
}


//========================= NODE FUNCTIONS ============================//

// Done: Alex
string GlobalPlanner::getNewID(int xi, int yi){
	string id = std::to_string(xi) + "_" +  std::to_string(yi);
	return id;	
}

// Done: Alex
float GlobalPlanner::edgeCost(const Node &node_A, const Node &node_B){
	// Basic distance cost function
	return((node_A.loc - node_B.loc).norm());
}

// Helper Function (untested)
// outputs 2 lines parallel to edge that are displaced by a given offset
std::array<line2f,4> GlobalPlanner::getCushionLines(line2f edge, float offset){
	std::array<line2f, 4> bounding_box;
	Vector2f edge_unit_vector = (edge.p1 - edge.p0)/(edge.p1 - edge.p0).norm();
	Vector2f extended_edge = edge.p1 + offset* edge_unit_vector;   

	Vector2f normal_vec = edge.UnitNormal();
	Vector2f cushion_A_point_1 = edge.p0 + normal_vec * offset;
	Vector2f cushion_A_point_2 = extended_edge + normal_vec * offset;
	Vector2f cushion_B_point_1 = edge.p0 - normal_vec * offset;
	Vector2f cushion_B_point_2 = extended_edge - normal_vec * offset;

	bounding_box[0] = line2f(cushion_A_point_1, cushion_A_point_2);
	bounding_box[1] = line2f(cushion_B_point_1, cushion_B_point_2);
	bounding_box[2] = line2f(cushion_A_point_1, cushion_B_point_1);
	bounding_box[3] = line2f(cushion_A_point_2, cushion_B_point_2);
	return bounding_box;
}

// Done: Alex
bool GlobalPlanner::isValidNeighbor(const Node &node, const Neighbor &neighbor){
	// Check for adjacency
	int x_offset = node.index.x() - neighbor.node_index.x();
	int y_offset = node.index.y() - neighbor.node_index.y();
	if (not (abs(x_offset) == 1 or abs(y_offset) == 1)) // changed so that node can't be neighbors with itself
		return false;

	// Create 3 lines: 1 from A to B and then the others offset from that as a cushion
	Vector2f offset(map_resolution_ * x_offset, map_resolution_ * y_offset);
	Vector2f neighbor_loc = node.loc + offset;
	const line2f edge(node.loc, neighbor_loc);
	auto cushion_lines = getCushionLines(edge, 0.5);

	// Check for collisions
	for (const line2f map_line : map_.lines)
    {
		bool intersection = map_line.Intersects(edge);
		for (const line2f bounding_box_edge : cushion_lines){
			intersection = intersection or map_line.Intersects(bounding_box_edge);
		}
		if (intersection) return false;
    }

	return true;
}

// Done: Alex
vector<Neighbor> GlobalPlanner::getNeighbors(const Node &node){
	vector<Neighbor> neighbors;
	vector<Neighbor> valid_neighbors;

	int xi = node.index.x();
	int yi = node.index.y();

	float diagonal_path_length = sqrt(2)*map_resolution_;	// Assuming a straight line path
	float straight_path_length = map_resolution_;					// Straight line distance

	// Note the each neighbor has the form {ID, curvature, path length, index}
	neighbors.push_back({Vector2i(xi-1, yi+1), getNewID(xi-1, yi+1), diagonal_path_length, 0});	// Left and up
	neighbors.push_back({Vector2i(xi  , yi+1), getNewID(xi,   yi+1), straight_path_length, 1});	// Directly up
	neighbors.push_back({Vector2i(xi+1, yi+1), getNewID(xi+1, yi+1), diagonal_path_length, 2});	// Right and up
	neighbors.push_back({Vector2i(xi-1, yi  ), getNewID(xi-1, yi  ), straight_path_length, 3});	// Directly left
	neighbors.push_back({Vector2i(xi+1, yi  ), getNewID(xi+1, yi  ), straight_path_length, 5});	// Directly right
	neighbors.push_back({Vector2i(xi-1, yi-1), getNewID(xi-1, yi-1), diagonal_path_length, 6}); // Left and down
	neighbors.push_back({Vector2i(xi  , yi-1), getNewID(xi,   yi-1), straight_path_length, 7});	// Directly down
	neighbors.push_back({Vector2i(xi+1, yi-1), getNewID(xi+1, yi-1), diagonal_path_length, 8});	// Right and down

	// Only pass valid neighbors
	for(size_t i=0; i<neighbors.size(); i++){
		if ( isValidNeighbor(node, neighbors[i]) ){
			valid_neighbors.push_back(neighbors[i]);
		}
	}

	return valid_neighbors;
}

// Done: Alex
Node GlobalPlanner::newNode(const Node &old_node, int neighbor_index){
	Node new_node;
	
	// Change in index, not in position
	int dx = (neighbor_index % 3 == 2) - (neighbor_index % 3 == 0);
	int dy = (neighbor_index < 3) - (neighbor_index > 5);

	new_node.loc         = old_node.loc + map_resolution_ * Vector2f(dx, dy);
	new_node.index       = old_node.index + Vector2i(dx, dy);
	new_node.cost        = old_node.cost + edgeCost(old_node, new_node);
	new_node.social_cost = getSocialCost(new_node);
	new_node.parent      = old_node.key;
	new_node.key         = getNewID(new_node.index.x(), new_node.index.y());
	new_node.neighbors   = getNeighbors(new_node);

	for (const auto &bad_loc : failed_locs_){
		if ((new_node.loc - bad_loc).norm() < map_resolution_*3){
			new_node.neighbors.clear();
			break;
		}
	}

	// Add to node map with key
	nav_map_[new_node.key] = new_node;

	return new_node;
}

// Done: Alex
void GlobalPlanner::initializeMap(Eigen::Vector2f loc){
	nav_map_.clear();
	frontier_.Clear();

	int xi = loc.x()/map_resolution_;
	int yi = loc.y()/map_resolution_;

	Node start_node;
	start_node.loc 	  = loc;
	start_node.index  = Eigen::Vector2i(xi, yi);
	start_node.cost   = 0;
	start_node.social_cost = 0;
	start_node.social_type = 'n';
	start_node.parent = "START";
	start_node.key    = "START";
	start_node.neighbors = getNeighbors(start_node);

	nav_map_[start_node.key] = start_node;
	frontier_.Push("START", 0.0);
}


//====================== HUMAN MANIPULATION ==========================//
void GlobalPlanner::addHuman(human::Human* Bob){
	// Mark that a replan is needed
	if (not global_path_.empty()) {
		need_social_replan_ = true;
	}

	// Add human to planner
	population_.push_back(Bob);
	population_locs_.push_back(Bob->getLoc());
	population_angles_.push_back(Bob->getAngle());
}

void GlobalPlanner::clearPopulation(){
	population_.clear();
}

bool GlobalPlanner::needSocialReplan(Eigen::Vector2f robot_loc){
	if (need_social_replan_) return true;

	for (size_t i = 0; i < population_.size(); i++){
		const human::Human &person = *population_[i];
		if (person.isHidden(robot_loc, map_)) continue; // Do not replan if we cannot see the human

		bool human_moved  = (person.getLoc() - population_locs_[i]).norm() > 0.5;
		bool human_turned = abs(math_util::AngleDiff(person.getAngle(), population_angles_[i])) > 0.5;
		need_social_replan_ = need_social_replan_ or human_moved or human_turned;

		if (human_moved)  population_locs_[i]   = person.getLoc();
		if (human_turned) population_angles_[i] = person.getAngle();
	}

	return need_social_replan_;
}


//========================= PATH PLANNING ============================//

float GlobalPlanner::getSocialCost(Node &new_node){
	float safety_cost = 0;
	float visibility_cost = 0;
	float hidden_cost = 0;
	float max_social_cost = 0;
	// 'n' is none, 's' is safety, 'v' is visibility, 'h' is hidden
	char social_type = 'n';

	for(auto &H : population_){
		// Skip if node is further than 10m from this human
		if ( (new_node.loc - H->getLoc()).norm() > 10 ) continue;
		
		// If node is hidden behind wall, return surprise factor
		if ( H->isHidden(new_node.loc, map_) ){
			// Line of sight from human to node
			const line2f view_line(H->getLoc(), new_node.loc);
			for (const line2f map_line : map_.lines){
				Vector2f intersection_point;
				bool intersects = map_line.Intersection(view_line, &intersection_point);
				if (intersects){
					// hiddenCost also checks if node is in FOV with private isVisible
					hidden_cost = H->hiddenCost(new_node.loc, intersection_point);
					if (hidden_cost > max_social_cost){
						max_social_cost = hidden_cost;
						social_type = 'h';
					}
				}
			}
		}
		// Otherwise, return safety or visibility factor, whichever is higher
		else{
			safety_cost = H->safetyCost(new_node.loc);
			visibility_cost = H->visibilityCost(new_node.loc);
			float social_cost = std::max(safety_cost, visibility_cost);
			if (social_cost > max_social_cost){
				max_social_cost = social_cost;
				social_type = (safety_cost > visibility_cost) ? 's' : 'v';
			}
		}
	}
	new_node.social_type = social_type;
	// Scale by arbitrary factor to weight social costs with distance costs appropriately
	return max_social_cost;
}

void GlobalPlanner::getGlobalPath(Vector2f nav_goal_loc){
	nav_goal_ = nav_goal_loc;

	bool global_path_success = false;
	int loop_counter = 0; // exit condition if while loop gets stuck (goal unreachable)
	string current_key;
	while(!frontier_.Empty() && loop_counter < 1E6)
	{
		// Get key for the lowest-priority node in frontier_ and then remove it
		current_key = frontier_.Pop();
		Node current_node = nav_map_[current_key];

		// Are we there yet? (0.71 is sqrt(2)/2 with some added buffer)
		if ( (nav_goal_loc - current_node.loc).norm() < 0.71*map_resolution_ )
		{
			global_path_success = true;
			break;
		}

		for(auto &next_neighbor : current_node.neighbors)
		{
			string neighbor_id = next_neighbor.key;
			bool unexplored = !nav_map_.count(next_neighbor.key);
			float neighbor_cost = current_node.cost + next_neighbor.path_length;

			// Is this the first time we've seen this node?
			if (unexplored){
				// Make new Node out of neighbor
				Node new_node = newNode(current_node, next_neighbor.neighbor_index);
				neighbor_cost += new_node.social_cost;
				float heuristic = 1.0*getHeuristic(nav_goal_loc, new_node.loc);
				frontier_.Push(neighbor_id, neighbor_cost+heuristic);
			
			}else if (neighbor_cost < nav_map_[neighbor_id].cost){
				nav_map_[neighbor_id].cost = neighbor_cost;
				nav_map_[neighbor_id].parent = current_node.key;
				neighbor_cost += nav_map_[neighbor_id].social_cost;
				float heuristic = 1.0*getHeuristic(nav_goal_loc, nav_map_[neighbor_id].loc);
				frontier_.Push(neighbor_id, neighbor_cost+heuristic);
			}
		}
		loop_counter++;
	}

	vector<string> global_path;
	if (global_path_success){
		cout << "After " << loop_counter << " iterations, global path success!" << endl;
		// Backtrace optimal A* path
		string path_key = current_key;
		float total_dist_travelled = 0;
		while (path_key != "START"){
			global_path.push_back(path_key);
			total_dist_travelled += edgeCost(nav_map_[path_key], nav_map_[nav_map_[path_key].parent]);
			path_key = nav_map_[path_key].parent;
		}
		cout << "Travelled " << total_dist_travelled << "m" << endl;
		// If you want to go from start to goal:
		std::reverse(global_path.begin(), global_path.end());
	}
	else{
		cout << "After " << loop_counter << " iterations, global path failure." << endl;
		global_path.push_back("START");
	}

	global_path_ = global_path;
}

float GlobalPlanner::getHeuristic(const Vector2f &goal_loc, const Vector2f &node_loc){
	Vector2f abs_diff_loc = (goal_loc - node_loc).cwiseAbs();
	// 4-grid heuristic is just Manhattan distance
	// float heuristic = abs_diff_loc.x() + abs_diff_loc.y();

	// 2-norm doesn't seem to work either
	// float heuristic = abs_diff_loc.norm();

	// 8-grid heuristic is a little bit complex
	float straight_length = std::abs(abs_diff_loc.x()-abs_diff_loc.y());
	float diag_length = sqrt(2)*(abs_diff_loc.x()+abs_diff_loc.y()-straight_length)*0.5;
	float heuristic = straight_length + diag_length;

	// No hueristic
	// float hueristic = 0;

	return heuristic;
}

// In-work: Connor 
// Post: will actually need to pass in the node location to the drive along global path function
Node GlobalPlanner::getClosestPathNode(Eigen::Vector2f robot_loc, amrl_msgs::VisualizationMsg &msg){
	// Initialize output
	Node target_node;
	int target_index = 0;
	Node closest_node;
	int closest_index = 0;

	// Draw Circle around Robot's Location that will Intersect with Global Path
	float circle_rad_min = 2.0;
	visualization::DrawArc(robot_loc,circle_rad_min,0.0,2*M_PI,0x909090, msg);

	// Find the closest node to the robot
	float min_distance = 100;
	for (size_t i = 0; i < global_path_.size(); i++)
	{
		Vector2f node_loc = nav_map_[global_path_[i]].loc;
		float dist_to_node_loc = (robot_loc-node_loc).norm();

		if (dist_to_node_loc < min_distance){
			min_distance = dist_to_node_loc;
			closest_node = nav_map_[global_path_[i]];
			closest_index = i;
		}
	}
	closest_node.visited = true;

	// Check if the closest node is outside circle radius
	need_replan_ = (min_distance > circle_rad_min ? true : false);
	if (need_replan_) {return closest_node;}

	// Extract the first node after the closest node that is outside the circle
	for(size_t i = closest_index; i < global_path_.size(); i++)
	{
		target_node = nav_map_[global_path_[i]];
		float dist_to_node_loc = (robot_loc - target_node.loc).norm();

		if (dist_to_node_loc > circle_rad_min) {
			target_index = i;
			break;
		}
	}

	// If there is a clear path between the robot and the goal then
	// choose this goal node. If not, step back and keep checking
	for(int i = target_index; i > closest_index; i--){
		Vector2f target_loc = nav_map_[global_path_[i]].loc;
		line2f car_to_goal(robot_loc, target_loc);

		visualization::DrawLine(robot_loc, target_loc, 0x000000, msg);

		bool intersection = map_.Intersects(robot_loc, target_loc);
		if (!intersection){
			target_node = nav_map_[global_path_[i]];
			return target_node;
		}

		if (i < closest_index + 4) { //within a meter
			cout << "!";
			need_replan_ = true;
			return target_node;
		}
	}

	return target_node;
}

bool GlobalPlanner::needsReplan(){return need_replan_;}

void GlobalPlanner::replan(Vector2f robot_loc, Vector2f failed_target_loc){
	
	if ( (robot_loc - failed_target_loc).norm() > 1.41*map_resolution_)	// 1.41 for sqrt(2)
		failed_locs_.push_back(failed_target_loc);
	
	initializeMap(robot_loc);
	getGlobalPath(nav_goal_);

	cout << "replanning and avoiding nodes at:" << endl;
	for (auto &l : failed_locs_){
		cout << "(" << l.x() << ", " << l.y() << ")" << endl;
	}
	cout << endl;

	need_replan_ = false;
	need_social_replan_ = false;
}


//========================= VISUALIZATION ============================//

void GlobalPlanner::plotGlobalPath(amrl_msgs::VisualizationMsg &msg){
	if (global_path_.empty()) return;

	Vector2f start = nav_map_[global_path_.front()].loc;
	Vector2f goal = nav_map_[global_path_.back()].loc;
	visualization::DrawCross(start, 0.5, 0xff0000, msg);
	visualization::DrawCross(goal, 0.5, 0xff0000, msg);

	for (auto key = global_path_.begin(); key != global_path_.end(); key++){
		string end_key = nav_map_[*key].parent;
		Vector2f start_loc = nav_map_[*key].loc;
		Vector2f end_loc = nav_map_[end_key].loc;
		visualization::DrawLine(start_loc, end_loc, 0x009c08, msg);
	}
}

// Done: Connor
void GlobalPlanner::plotSocialCosts(amrl_msgs::VisualizationMsg &msg){
	// Iterate through every explored node
	for(const auto &element : nav_map_){
		const Vector2f node_loc = element.second.loc;
		const char social_type = element.second.social_type;
		float social_cost = element.second.social_cost;
		if (social_cost > 1.0) social_cost = 1.0;
		if (social_cost < 0.5) social_cost = 0.5;
		const int color_shade = 255*(1-social_cost);
		float vis_color = 0;
		switch(social_type){
			case 's':
				vis_color = 255*pow(16,4)+color_shade*(pow(16,2)+1);
				break;
			case 'v':
				vis_color = 255*pow(16,2)+color_shade*(pow(16,4)+1);
				break;
			case 'h':
				vis_color = 255+color_shade*(pow(16,4)+pow(16,2));
				break;
			default:
				vis_color = 0xcccccc;
		}
		visualization::DrawPoint(node_loc, vis_color, msg);
	}
}

void GlobalPlanner::plotFrontier(amrl_msgs::VisualizationMsg &msg){
	while(!frontier_.Empty()){
		string frontier_key = frontier_.Pop();
		Vector2f frontier_loc = nav_map_[frontier_key].loc;
		visualization::DrawPoint(frontier_loc, 0x0000ff, msg);
	}
}

// Done: Connor
void GlobalPlanner::plotNodeNeighbors(const Node &node, amrl_msgs::VisualizationMsg &msg){
	// Visualize the node and it's immediate neighbors

	visualization::DrawCross(node.loc,2.0,0xff0000,msg);
	for (size_t i = 0; i < node.neighbors.size(); i++){
		// Get the ID for this neighboring node
		string neighbor_id = node.neighbors[i].key;
		int neighbor_index = node.neighbors[i].neighbor_index;

		// Find the location of the neighbor
		int dx = (neighbor_index % 3 == 2) - (neighbor_index % 3 == 0);
		int dy = (neighbor_index < 3) - (neighbor_index > 5);
		Vector2f neighbor_loc = node.loc + map_resolution_ * Vector2f(dx, dy);

		// Visualize
		visualization::DrawPoint(neighbor_loc,0xff9900,msg);
		visualization::DrawLine(node.loc, neighbor_loc, 0x000dff, msg);		
	}
}

void GlobalPlanner::plotInvalidNodes(amrl_msgs::VisualizationMsg &msg){
	for (const Vector2f &loc : failed_locs_){
		visualization::DrawCross(loc, 0.5, 0x000000, msg);
	}
}
