/*
	Copyright May 7, 2014 Southwest Research Institute

	Licensed under the Apache License, Version 2.0 (the "License");
	you may not use this file except in compliance with the License.
	You may obtain a copy of the License at

		http://www.apache.org/licenses/LICENSE-2.0

	Unless required by applicable law or agreed to in writing, software
	distributed under the License is distributed on an "AS IS" BASIS,
	WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
	See the License for the specific language governing permissions and
	limitations under the License.
*/

#include <godel_surface_detection/scan/robot_scan.h>
#include <godel_surface_detection/detection/surface_detection.h>
#include <godel_surface_detection/interactive/interactive_surface_server.h>
#include <godel_msgs/SurfaceDetection.h>
#include <godel_msgs/SelectSurface.h>
#include <godel_msgs/SelectedSurfacesChanged.h>
#include <godel_msgs/ProcessPlanning.h>
#include <godel_msgs/SurfaceBlendingParameters.h>
#include <godel_process_path_generation/VisualizeBlendingPlan.h>
#include <godel_process_path_generation/mesh_importer.h>
#include <godel_process_path_generation/utils.h>
#include <pcl/console/parse.h>

// topics and services
const std::string SURFACE_DETECTION_SERVICE = "surface_detection";
const std::string SURFACE_BLENDING_PARAMETERS_SERVICE = "surface_blending_parameters";
const std::string SELECT_SURFACE_SERVICE = "select_surface";
const std::string PROCESS_PATH_SERVICE="process_path";
const std::string VISUALIZE_BLENDING_PATH_SERVICE = "visualize_path_generator";
const std::string TOOL_PATH_PREVIEW_TOPIC = "tool_path_preview";
const std::string SELECTED_SURFACES_CHANGED_TOPIC = "selected_surfaces_changed";
const std::string ROBOT_SCAN_PATH_PREVIEW_TOPIC = "robot_scan_path_preview";
const std::string PUBLISH_REGION_POINT_CLOUD = "publish_region_point_cloud";
const std::string REGION_POINT_CLOUD_TOPIC="region_colored_cloud";

// marker namespaces
const std::string BOUNDARY_NAMESPACE = "process_boundary";
const std::string PATH_NAMESPACE = "process_path";
const std::string TOOL_NAMESPACE = "process_tool";

// tool visual properties
const float TOOL_DIA = .050;
const float TOOL_THK = .005;
const float TOOL_SHAFT_DIA = .006;
const float TOOL_SHAFT_LEN = .045;
const std::string TOOL_FRAME_ID = "process_tool";

struct ProcessPathDetails
{
	visualization_msgs::MarkerArray process_boundaries_;
	visualization_msgs::MarkerArray process_paths_;
	visualization_msgs::MarkerArray tool_parts_;
};

class SurfaceDetectionService
{
public:
	SurfaceDetectionService():
		publish_region_point_cloud_(false),
	        mesh_importer_(true) /*True-turn on verbose messages*/
	{

	}

	~SurfaceDetectionService()
	{

	}

	bool init()
	{
		using namespace godel_surface_detection;

		ros::NodeHandle ph("~");

		// loading parameters
		ph.getParam(PUBLISH_REGION_POINT_CLOUD,publish_region_point_cloud_);

		// initializing surface detector
		if(surface_detection_.load_parameters("~/surface_detection") &&
				robot_scan_.load_parameters("~/robot_scan") &&
				load_blending_parameters("~/blending_plan",blending_plan_params_) &&
				surface_server_.load_parameters())
		{
			// save default parameters
			default_robot_scan_params__ = robot_scan_.params_;
			default_surf_detection_params_ = surface_detection_.params_;
			default_blending_plan_params_ = blending_plan_params_;


			ROS_INFO_STREAM("Surface detection service loaded parameters successfully");
			if(surface_detection_.init() && robot_scan_.init() && surface_server_.init())
			{
				// adding callbacks
				scan::RobotScan::ScanCallback cb = boost::bind(&detection::SurfaceDetection::add_cloud,&surface_detection_,_1);
				robot_scan_.add_scan_callback(cb);
				ROS_INFO_STREAM("Surface detection service initialization succeeded");
			}
			else
			{
				ROS_ERROR_STREAM("Surface detection service had an initialization error");
			}

		}
		else
		{
			ROS_ERROR_STREAM("Surface detection service failed to load parameters");
		}

		// start server
		interactive::InteractiveSurfaceServer::SelectionCallback f =	boost::bind(
				&SurfaceDetectionService::publish_selected_surfaces_changed,this);
		surface_server_.add_selection_callback(f);

		// initializing ros interface
		ros::NodeHandle nh;

		// service clients
		visualize_process_path_client_ =
				nh.serviceClient<godel_process_path_generation::VisualizeBlendingPlan>(VISUALIZE_BLENDING_PATH_SERVICE);

		// service servers
		surf_blend_parameters_server_ = nh.advertiseService(SURFACE_BLENDING_PARAMETERS_SERVICE,
				&SurfaceDetectionService::surface_blend_parameters_server_callback,this);

		surface_detect_server_ = nh.advertiseService(SURFACE_DETECTION_SERVICE,
				&SurfaceDetectionService::surface_detection_server_callback,this);

		select_surface_server_ = nh.advertiseService(SELECT_SURFACE_SERVICE,
				&SurfaceDetectionService::select_surface_server_callback,this);

		process_path_server_ = nh.advertiseService(PROCESS_PATH_SERVICE,
				&SurfaceDetectionService::process_path_server_callback,this);

		// publishers
		selected_surf_changed_pub_ = nh.advertise<godel_msgs::SelectedSurfacesChanged>(SELECTED_SURFACES_CHANGED_TOPIC,1);

		point_cloud_pub_ = nh.advertise<sensor_msgs::PointCloud2>(REGION_POINT_CLOUD_TOPIC,1);

		tool_path_markers_pub_ = nh.advertise<visualization_msgs::MarkerArray>(TOOL_PATH_PREVIEW_TOPIC,1);

		// timers
		tool_animation_timer_ = nh.createTimer(ros::Duration(0.1f),&SurfaceDetectionService::tool_animation_timer_callback,this,true,false);

		return true;
	}

	void run()
	{
		surface_server_.run();


		ros::Duration loop_duration(1.0f);
		while(ros::ok())
		{
			if(publish_region_point_cloud_ && !region_cloud_msg_.data.empty())
			{
				point_cloud_pub_.publish(region_cloud_msg_);
			}

			loop_duration.sleep();
		}
	}

protected:

	bool load_blending_parameters(std::string ns,godel_msgs::BlendingPlanParameters& params)
	{
		ros::NodeHandle nh(ns);
		return nh.getParam("tool_radius",params.tool_radius) &&
				nh.getParam("margin",params.margin) &&
				nh.getParam("overlap",params.overlap) &&
				nh.getParam("approach_spd",params.approach_spd) &&
				nh.getParam("blending_spd",params.blending_spd) &&
				nh.getParam("retract_spd",params.retract_spd) &&
				nh.getParam("traverse_spd",params.traverse_spd) &&
				nh.getParam("discretization",params.discretization) &&
				nh.getParam("safe_traverse_height",params.safe_traverse_height);
	}

	void publish_selected_surfaces_changed()
	{
		godel_msgs::SelectedSurfacesChanged msg;
		msg.selected_surfaces.clear();
		surface_server_.get_selected_list(msg.selected_surfaces);
		selected_surf_changed_pub_.publish(msg);
	}

	bool run_robot_scan(visualization_msgs::MarkerArray &surfaces)
	{
		bool succeeded = true;

		// publishing scan path preview
		robot_scan_.publish_scan_poses(ROBOT_SCAN_PATH_PREVIEW_TOPIC);

		// clear all results
		surface_detection_.clear_results();

		// saving parameters used

		ROS_INFO_STREAM("Starting scan");

		int scans_completed = robot_scan_.scan(false);
		if(scans_completed > 0)
		{
			ROS_INFO_STREAM("Scan points reached "<<scans_completed);
			succeeded = find_surfaces(surfaces);
		}
		else
		{
			succeeded = false;
			ROS_ERROR_STREAM("Scan failed");
		}
		return succeeded;
	}

	bool find_surfaces(visualization_msgs::MarkerArray &surfaces)
	{
		bool succeeded = true;
		if(surface_detection_.find_surfaces())
		{
			// clear current surfaces
			surface_server_.remove_all_surfaces();

			// adding meshes to server
			std::vector<pcl::PolygonMesh> meshes;
			surface_detection_.get_meshes(meshes);
			for(int i =0;i < meshes.size();i++)
			{
				surface_server_.add_surface(meshes[i]);
			}

			// copying to surface markers to output argument
			visualization_msgs::MarkerArray markers_msg = surface_detection_.get_surface_markers();
			surfaces.markers.insert(surfaces.markers.begin(),markers_msg.markers.begin(),markers_msg.markers.end());

			// saving latest successful results
			latest_surface_detection_results_.surface_detection = surface_detection_.params_;
			latest_surface_detection_results_.surfaces_found = true;
			latest_surface_detection_results_.surfaces = surfaces;
			robot_scan_.get_latest_scan_poses(latest_surface_detection_results_.robot_scan_poses);

			// saving region colored point cloud
			region_cloud_msg_ = sensor_msgs::PointCloud2();
			surface_detection_.get_region_colored_cloud(region_cloud_msg_);
		}
		else
		{
			succeeded = false;
			region_cloud_msg_ = sensor_msgs::PointCloud2();
		}

		return succeeded;
	}

	bool generate_process_plan(godel_process_path_generation::VisualizeBlendingPlan &process_plan)
	{
		// creating color structures for the different parts for the process path
		std_msgs::ColorRGBA yellow;
		yellow.a = 1.f;
		yellow.b = 0.f;
		yellow.r = yellow.g = 1.f;

		// saving parameters
		blending_plan_params_ = process_plan.request.params;

		// clear previous results
		process_path_results_.process_boundaries_.markers.clear();
		process_path_results_.process_paths_.markers.clear();
		process_path_results_.tool_parts_.markers.clear();

		// generating boundaries
		std::vector<pcl::PolygonMesh> meshes;
		surface_server_.get_selected_surfaces(meshes);
		std::vector<geometry_msgs::Polygon> &boundaries = process_plan.request.surface.boundaries;

		int boundaries_found = 0; // successful computations counter
		int marker_counter = 0;
		for(int i =0;i <meshes.size();i++)
		{
			// markers for saving individual mesh results
			visualization_msgs::MarkerArray boundary_markers;
			geometry_msgs::Pose boundary_pose;
			visualization_msgs::Marker path_marker;
			const pcl::PolygonMesh &mesh = meshes[i];
			if(!mesh_importer_.calculateBoundaryData(mesh))
			{
				// add boundaries to request
				boundaries.clear();
				godel_process_path::utils::translations::godelToGeometryMsgs(boundaries,mesh_importer_.getBoundaries());
				tf::poseTFToMsg(tf::Transform::getIdentity(),process_plan.request.surface.pose);

				// calling visualization service and saving results
				if(visualize_process_path_client_.call(process_plan))
				{
					// create boundaries markers
					godel_process_path::utils::translations::godelToVisualizationMsgs(boundary_markers,mesh_importer_.getBoundaries()
							,yellow);
					mesh_importer_.getPose(boundary_pose);

					// setting boundary marker properties
					for(int j =0; j < boundary_markers.markers.size();j++)
					{
						visualization_msgs::Marker &m = boundary_markers.markers[j];
						m.header.frame_id = meshes[i].header.frame_id;
						m.id = marker_counter;
						m.lifetime = ros::Duration(0);
						m.ns = BOUNDARY_NAMESPACE;
						m.pose = boundary_pose;

						marker_counter++;
					}

					// create path marker
					path_marker = process_plan.response.path;
					path_marker.id = marker_counter;
					path_marker.lifetime = ros::Duration(0);
					path_marker.ns = PATH_NAMESPACE;
					path_marker.pose = boundary_pose;

					// saving into results structure;
					process_path_results_.process_boundaries_.markers.insert(process_path_results_.process_boundaries_.markers.end(),
							boundary_markers.markers.begin(),boundary_markers.markers.end());
					process_path_results_.process_paths_.markers.push_back(path_marker);

					boundaries_found++;
					marker_counter++;
				}
			}
		}


		return boundaries_found >0;
	}

	bool animate_tool_path()
	{
		bool succeeded = !process_path_results_.process_paths_.markers.empty();
		stop_tool_animation_ = true;
		ros::Duration(0.5f).sleep();

		if(succeeded)
		{

			ROS_INFO_STREAM("tool path animation started");
			tool_animation_timer_.start();
		}
		else
		{
			ROS_WARN_STREAM("No tool path markers found, exiting animation routine");
		}

		return succeeded;
	}

	void tool_animation_timer_callback(const ros::TimerEvent& event)
	{
		stop_tool_animation_ = false;

		// path progress color
		std_msgs::ColorRGBA green;
		green.a = 1.f;
		green.g = 1.f;
		green.r = green.b = 0.f;

		// marker array for all markers
		visualization_msgs::MarkerArray process_markers ;

		// tool marker at arbitrary position
		visualization_msgs::MarkerArray tool_markers = create_tool_markers(geometry_msgs::Point(),geometry_msgs::Pose(),"");


		// adding markers
		int num_path_markers = process_path_results_.process_paths_.markers.size();
		int num_tool_markers = tool_markers.markers.size();
		process_markers.markers.insert(process_markers.markers.end(),process_path_results_.process_paths_.markers.begin(),
				process_path_results_.process_paths_.markers.end());
		process_markers.markers.insert(process_markers.markers.end(),process_path_results_.process_boundaries_.markers.begin(),
						process_path_results_.process_boundaries_.markers.end());
		process_markers.markers.insert(process_markers.markers.end(),tool_markers.markers.begin(),
				tool_markers.markers.end());

		ros::Duration loop_pause(0.2f);
		for(int i = 0;i < process_path_results_.process_paths_.markers.size();i++)
		{
			visualization_msgs::Marker &path_marker = process_markers.markers[i];

			for(int j =0;j < path_marker.points.size();j++)
			{
				if(stop_tool_animation_)
				{
					ROS_WARN_STREAM("tool path animation completed");
					return;
				}

				// updating path color at current point
				path_marker.colors[j] = green;

				// updating tool markers
				tool_markers = create_tool_markers(path_marker.points[j],path_marker.pose,path_marker.header.frame_id);
				int start_tool_marker_index = process_markers.markers.size() - tool_markers.markers.size();

				process_markers.markers.insert(process_markers.markers.begin()+start_tool_marker_index,
						tool_markers.markers.begin(),tool_markers.markers.end());

				// publish marker array
				tool_path_markers_pub_.publish(process_markers);

				loop_pause.sleep();
			}
		}

		ROS_INFO_STREAM("tool path animation completed");
	}

	visualization_msgs::MarkerArray create_tool_markers(const geometry_msgs::Point &pos, const geometry_msgs::Pose &pose,std::string frame_id)
	{
		visualization_msgs::MarkerArray tool;
		tool.markers.resize(2);
		visualization_msgs::Marker &disk = tool.markers.at(0);
		visualization_msgs::Marker &shaft = tool.markers.at(1);

		std_msgs::ColorRGBA blue;
		blue.r = 0.;
		blue.g = .1;
		blue.b = 1.;
		blue.a = 0.7;

		disk.action = visualization_msgs::Marker::ADD;
		disk.color = blue;
		disk.frame_locked = true;
		disk.header.frame_id = frame_id;
		disk.header.seq = 0;
		disk.header.stamp = ros::Time::now();
		disk.lifetime = ros::Duration(0.);
		disk.pose = pose;
		disk.ns = TOOL_NAMESPACE;
		// disk/shaft position filled out below
		disk.type = visualization_msgs::Marker::CYLINDER;
		shaft = disk;

		tf::Transform marker_pose;
		tf::poseMsgToTF(pose, marker_pose);

		disk.id = 0;
		tf::Vector3 marker_pos(pos.x, pos.y, pos.z + .5*TOOL_THK);
		marker_pos = marker_pose * marker_pos;
		tf::pointTFToMsg(marker_pos, disk.pose.position);
		disk.scale.x = disk.scale.y = TOOL_DIA;
		disk.scale.z = TOOL_THK;

		shaft.id = 1;
		marker_pos = tf::Vector3(pos.x, pos.y, pos.z + TOOL_THK + 0.5*TOOL_SHAFT_LEN);
		marker_pos = marker_pose * marker_pos;
		tf::pointTFToMsg(marker_pos, shaft.pose.position);
		shaft.scale.x = shaft.scale.y = TOOL_SHAFT_DIA;
		shaft.scale.z = TOOL_SHAFT_LEN;

		return tool;
	}

	bool surface_detection_server_callback(godel_msgs::SurfaceDetection::Request &req,
			godel_msgs::SurfaceDetection::Response &res)
	{

		res.surfaces_found = false;
		res.surfaces = visualization_msgs::MarkerArray();

		switch(req.action)
		{
		case req.GET_CURRENT_PARAMETERS:
			res.robot_scan = robot_scan_.params_;
			res.surface_detection = surface_detection_.params_;
			break;

		case req.GET_DEFAULT_PARAMETERS:
			res.robot_scan = default_robot_scan_params__;
			res.surface_detection = default_surf_detection_params_;
			break;

		case req.PUBLISH_SCAN_PATH:

			if(req.use_default_parameters)
			{
				robot_scan_.params_ = default_robot_scan_params__;
			}
			else
			{
				robot_scan_.params_ = req.robot_scan;
			}

			robot_scan_.publish_scan_poses(ROBOT_SCAN_PATH_PREVIEW_TOPIC);
			break;

		case req.SCAN_AND_FIND_ONLY:

			if(req.use_default_parameters)
			{
				robot_scan_.params_ = default_robot_scan_params__;
				surface_detection_.params_ = default_surf_detection_params_;
			}
			else
			{
				robot_scan_.params_ = req.robot_scan;
				surface_detection_.params_ = req.surface_detection;
			}

			res.surfaces_found =  run_robot_scan(res.surfaces);
			res.surfaces.markers.clear();
			break;

		case req.SCAN_FIND_AND_RETURN:

			if(req.use_default_parameters)
			{
				robot_scan_.params_ = default_robot_scan_params__;
				surface_detection_.params_ = default_surf_detection_params_;
			}
			else
			{
				robot_scan_.params_ = req.robot_scan;
				surface_detection_.params_ = req.surface_detection;
			}

			res.surfaces_found =  run_robot_scan(res.surfaces);
			break;

		case req.FIND_ONLY:

			if(req.use_default_parameters)
			{
				surface_detection_.params_ = default_surf_detection_params_;
			}
			else
			{
				surface_detection_.params_ = req.surface_detection;
			}

			res.surfaces_found =  find_surfaces(res.surfaces);
			res.surfaces.markers.clear();
			break;

		case req.FIND_AND_RETURN:

			if(req.use_default_parameters)
			{
				surface_detection_.params_ = default_surf_detection_params_;
			}
			else
			{
				surface_detection_.params_ = req.surface_detection;
			}

			res.surfaces_found =  find_surfaces(res.surfaces);
			break;

		case req.RETURN_LATEST_RESULTS:

			res = latest_surface_detection_results_;
			break;

		}

		return true;
	}

	bool select_surface_server_callback(godel_msgs::SelectSurface::Request &req, godel_msgs::SelectSurface::Response &res)
	{
		switch(req.action)
		{
		case req.SELECT:

			for(int i = 0; req.select_surfaces.size();i++)
			{
				surface_server_.set_selection_flag(req.select_surfaces[i],true);
			}
			break;

		case req.DESELECT:

			for(int i = 0; req.select_surfaces.size();i++)
			{
				surface_server_.set_selection_flag(req.select_surfaces[i],false);
			}
			break;

		case req.SELECT_ALL:

			surface_server_.select_all(true);
			break;

		case req.DESELECT_ALL:

			surface_server_.select_all(false);
			break;

		case req.HIDE_ALL:

			surface_server_.show_all(false);
			break;

		case req.SHOW_ALL:
			surface_server_.show_all(true);
			break;
		}

		return true;
	}

	bool process_path_server_callback(godel_msgs::ProcessPlanning::Request &req, godel_msgs::ProcessPlanning::Response &res)
	{
		godel_process_path_generation::VisualizeBlendingPlan process_plan;
		process_plan.request.params = req.use_default_parameters ? default_blending_plan_params_: req.params;
		switch(req.action)
		{
			case req.GENERATE_MOTION_PLAN:
				res.succeeded = generate_process_plan(process_plan);
				break;

			case req.GENERATE_MOTION_PLAN_AND_PREVIEW:

				res.succeeded = generate_process_plan(process_plan) && animate_tool_path();
				break;

			case req.PREVIEW_TOOL_PATH:

				res.succeeded = animate_tool_path();
				break;

			case req.PREVIEW_MOTION_PLAN:

				res.succeeded = false;
				break;

			case req.EXECUTE_MOTION_PLAN:

				res.succeeded = false;
				break;

			default:

				ROS_ERROR_STREAM("Unknown action code '"<<req.action<<"' request");
				break;
		}

		res.succeeded = false;
		return true;
	}

	bool surface_blend_parameters_server_callback(godel_msgs::SurfaceBlendingParameters::Request &req,
			godel_msgs::SurfaceBlendingParameters::Response &res)
	{
		switch(req.action)
		{
		case req.GET_CURRENT_PARAMETERS:

			res.surface_detection = surface_detection_.params_;
			res.robot_scan = robot_scan_.params_;
			res.blending_plan = blending_plan_params_;
			break;

		case req.GET_DEFAULT_PARAMETERS:

			res.surface_detection = default_surf_detection_params_;
			res.robot_scan = default_robot_scan_params__;
			res.blending_plan = default_blending_plan_params_;
			break;
		}

		return true;
	}

protected:

	ros::ServiceServer surface_detect_server_;
	ros::ServiceServer select_surface_server_;
	ros::ServiceServer process_path_server_;
	ros::ServiceServer surf_blend_parameters_server_;
	ros::ServiceClient visualize_process_path_client_;
	ros::Publisher selected_surf_changed_pub_;
	ros::Publisher point_cloud_pub_;
	ros::Publisher tool_path_markers_pub_;

	// timers
	ros::Timer tool_animation_timer_;
	bool stop_tool_animation_;

	// robot scan instance
	godel_surface_detection::scan::RobotScan robot_scan_;

	// surface detection instance
	godel_surface_detection::detection::SurfaceDetection surface_detection_;

	// marker server instance
	godel_surface_detection::interactive::InteractiveSurfaceServer surface_server_;

	// mesh importer for generating surface boundaries
	godel_process_path::MeshImporter mesh_importer_;


	// parameters
	godel_msgs::RobotScanParameters default_robot_scan_params__;
	godel_msgs::SurfaceDetectionParameters default_surf_detection_params_;
	godel_msgs::BlendingPlanParameters default_blending_plan_params_;
	godel_msgs::BlendingPlanParameters blending_plan_params_;

	// resutls
	godel_msgs::SurfaceDetection::Response latest_surface_detection_results_;
	ProcessPathDetails process_path_results_;

	// parameters
	bool publish_region_point_cloud_;

	// msgs
	sensor_msgs::PointCloud2 region_cloud_msg_;

};

int main(int argc,char** argv)
{
	ros::init(argc,argv,"surface_detection_server");
	ros::AsyncSpinner spinner(4);
	spinner.start();
	SurfaceDetectionService service;
	if(service.init())
	{
		service.run();
	}

	ros::waitForShutdown();
}

