/*
        Copyright Feb 11, 2014 Southwest Research Institute

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

#ifndef SURFACE_DETECTION_H_
#define SURFACE_DETECTION_H_

#include <ros/ros.h>
#include <sensor_msgs/PointCloud2.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/PolygonMesh.h>
#include <visualization_msgs/MarkerArray.h>
#include <godel_msgs/SurfaceDetectionParameters.h>

#include <random>

namespace godel_surface_detection
{
namespace detection
{

typedef pcl::PointCloud<pcl::PointXYZRGB> CloudRGB;
typedef pcl::PointCloud<pcl::Normal> Normals;

namespace defaults
{

// static const double ACQUISITION_TIME = 5.0f;
static std::string FRAME_ID = "world_frame";

static const int STATISTICAL_OUTLIER_MEAN = 50;
static const double STATISTICAL_OUTLIER_STDEV_THRESHOLD = 1;
static const int K_SEARCH = 50;

static const int REGION_GROWING_MIN_CLUSTER_SIZE = 100;
static const int REGION_GROWING_MAX_CLUSTER_SIZE = 100000;
static const int REGION_GROWING_NEIGHBORS = 50;
static const double REGION_GROWING_SMOOTHNESS_THRESHOLD = (M_PI / 180.0f) * 7.0f;
static const double REGION_GROWING_CURVATURE_THRESHOLD = 1.0f;

static const double TRIANGULATION_SEARCH_RADIUS = 0.01f;
static const double TRIANGULATION_MU = 2.5f;
static const int TRIANGULATION_MAX_NEAREST_NEIGHBORS = 100;
static const double TRIANGULATION_MAX_SURFACE_ANGLE = M_PI / 4.0f;
static const double TRIANGULATION_MIN_ANGLE = M_PI / 18.0f;
static const double TRIANGULATION_MAX_ANGLE = 2.0f * M_PI / 3.0f;
static const bool TRIANGULATION_NORMAL_CONSISTENCY = false;

static const bool PLANE_APROX_REFINEMENT_ENABLED = true;
static const int PLANE_APROX_REFINEMENT_SEG_MAX_ITERATIONS = 100;
static const double PLANE_APROX_REFINEMENT_SEG_DIST_THRESHOLD = 0.01f;
static const double PLANE_APROX_REFINEMENT_SAC_PLANE_DISTANCE = 0.01f;
static const std::string PLANE_APROX_REFINEMENT_KDTREE_RADIUS = "pa_kdtree_radius";

static const double VOXEL_LEAF_SIZE = 0.01f;

static const double OCCUPANCY_THRESHOLD = 0.1f;

// Moving least square smoothing
static const double MLS_UPSAMPLING_RADIUS = 0.01f;
static const double MLS_SEARCH_RADIUS = 0.01f;
static const int MLS_POINT_DENSITY = 40;

static const bool USE_TABLETOP_SEGMENTATION = true;
static const double TABLETOP_SEG_DISTANCE_THRESH = 0.005f;

static const double MARKER_ALPHA = 1.0f;
static const bool IGNORE_LARGEST_CLUSTER = false;
}

namespace config
{
static const std::string POINT_COLUD_TOPIC = "sensor_point_cloud";
}

namespace params
{
static const std::string FRAME_ID = "frame_id";
static const std::string USE_OCTOMAP = "use_octomap";

static const std::string STOUTLIER_MEAN = "stout_mean";
static const std::string STOUTLIER_STDEV_THRESHOLD = "stout_stdev_threshold";
static const std::string K_SEARCH = "k_search";

static const std::string REGION_GROWING_MIN_CLUSTER_SIZE = "rg_min_cluster_size";
static const std::string REGION_GROWING_MAX_CLUSTER_SIZE = "rg_max_cluster_size";
static const std::string REGION_GROWING_NEIGHBORS = "rg_neighbors";
static const std::string REGION_GROWING_SMOOTHNESS_THRESHOLD = "rg_smoothness_threshold";
static const std::string REGION_GROWING_CURVATURE_THRESHOLD = "rg_curvature_threshold";

static const std::string TRIANGULATION_SEARCH_RADIUS = "tr_search_radius";
static const std::string TRIANGULATION_MU = "tr_mu";
static const std::string TRIANGULATION_MAX_NEAREST_NEIGHBORS = "tr_nearest_neighbors";
static const std::string TRIANGULATION_MAX_SURFACE_ANGLE = "tr_max_surface_angle";
static const std::string TRIANGULATION_MIN_ANGLE = "tr_min_angle";
static const std::string TRIANGULATION_MAX_ANGLE = "tr_max_angle";
static const std::string TRIANGULATION_NORMAL_CONSISTENCY = "tr_normal_consistency";

static const std::string PLANE_APROX_REFINEMENT_ENABLED = "pa_enabled";
static const std::string PLANE_APROX_REFINEMENT_SEG_MAX_ITERATIONS = "pa_seg_max_iterations";
static const std::string PLANE_APROX_REFINEMENT_SEG_DIST_THRESHOLD = "pa_seg_dist_threshold";
static const std::string PLANE_APROX_REFINEMENT_SAC_PLANE_DISTANCE = "pa_sac_plane_distance";
static const std::string PLANE_APROX_REFINEMENT_KDTREE_RADIUS = "pa_kdtree_radius";

static const std::string VOXEL_LEAF_SIZE = "voxel_leaf";

static const std::string OCCUPANCY_THRESHOLD = "occupancy_threshold";

static const std::string MLS_UPSAMPLING_RADIUS = "mls_upsampling_radius";
static const std::string MLS_SEARCH_RADIUS = "mls_search_radius";
static const std::string MLS_POINT_DENSITY = "mls_point_density";

static const std::string USE_TABLETOP_SEGMENTATION = "use_tabletop_segmentation";
static const std::string TABLETOP_SEG_DISTANCE_THRESH = "tabletop_seg_distance_thresh";

static const std::string MARKER_ALPHA = "marker_alpha";
static const std::string IGNORE_LARGEST_CLUSTER = "ignore_largest_cluster";
}

class SurfaceDetection
{

public:
  SurfaceDetection();

public:
  bool init();

  bool load_parameters(const std::string& filename);
  void save_parameters(const std::string& filename);

  bool find_surfaces();

  static void mesh_to_marker(const pcl::PolygonMesh& mesh, visualization_msgs::Marker& marker);

  // adds point cloud to the occupancy grid, it performs no frame transformation
  void add_cloud(CloudRGB& cloud);
  int get_acquired_clouds_count();

  void clear_results();

  // retrieve results
  visualization_msgs::MarkerArray get_surface_markers();
  void get_meshes(std::vector<pcl::PolygonMesh>& meshes);
  void get_surface_clouds(std::vector<CloudRGB::Ptr>& surfaces);
  void get_full_cloud(CloudRGB& cloud);
  void get_full_cloud(sensor_msgs::PointCloud2 cloud_msg);
  void get_region_colored_cloud(CloudRGB& cloud);
  void get_region_colored_cloud(sensor_msgs::PointCloud2& cloud_msg);

  std::string getMeshingPluginName() const;


public:
  // parameters
  godel_msgs::SurfaceDetectionParameters params_;

private:
  // roscpp members
  ros::Subscriber point_cloud_subs_;

  std::default_random_engine random_engine_;

  // pcl members
  pcl::PointCloud<pcl::PointXYZRGB>::Ptr full_cloud_ptr_;
  CloudRGB::Ptr region_colored_cloud_ptr_;
  std::vector<CloudRGB::Ptr> surface_clouds_;
  visualization_msgs::MarkerArray mesh_markers_;
  std::vector<pcl::PolygonMesh> meshes_;

  // counter
  int acquired_clouds_counter_;
};
} /* end namespace detection */
} /* namespace godel_surface_detection */

#endif /* SURFACE_DETECTION_H_ */
