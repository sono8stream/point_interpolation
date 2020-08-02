#include <iostream>
#include <vector>
#include <stack>
#include <map>
#include <set>
#include <chrono>

#include <Open3D/Open3D.h>
#include <opencv2/opencv.hpp>
#include <Eigen/Core>
#include <Eigen/Eigen>
#include <Eigen/Sparse>
#include <eigen3/unsupported/Eigen/NonLinearOptimization>
#include <time.h>

using namespace std;
using namespace open3d;

const int width = 938;
const int height = 606;
//const int width = 882;
//const int height = 560;
const double f_x = width / 2 * 1.01;

ofstream ofs;

double PI = acos(-1);
double delta_rad = 0.52698 * PI / 180;
double max_rad = (16.6 + 0.26349) * PI / 180;

struct EnvParams
{
    int X;
    int Y;
    int Z;
    int roll;
    int pitch;
    int yaw;

    string folder_path;
    vector<int> data_ids;

    string of_name;
};

void calc_grid(shared_ptr<geometry::PointCloud> raw_pcd_ptr, EnvParams envParams,
               vector<vector<double>> &original_grid, vector<vector<double>> &filtered_grid,
               vector<vector<double>> &original_interpolate_grid, vector<vector<double>> &filtered_interpolate_grid,
               vector<vector<int>> &vs, int layer_cnt = 16)
{
    vector<double> tans;
    double PI = acos(-1);
    double delta_rad = 0.52698 * PI / 180;
    double max_rad = (16.6 + 0.26349) * PI / 180;
    double rad = (-16.6 + 0.26349) * PI / 180;
    while (rad < max_rad + 0.00001)
    {
        tans.emplace_back(tan(rad));
        rad += delta_rad;
    }

    vector<vector<Eigen::Vector3d>> all_layers(64, vector<Eigen::Vector3d>());
    for (int i = 0; i < raw_pcd_ptr->points_.size(); i++)
    {
        double rawX = raw_pcd_ptr->points_[i][1];
        double rawY = -raw_pcd_ptr->points_[i][2];
        double rawZ = -raw_pcd_ptr->points_[i][0];

        double r = sqrt(rawX * rawX + rawZ * rawZ);
        double rollVal = (envParams.roll - 500) / 1000.0;
        double pitchVal = (envParams.pitch - 500) / 1000.0;
        double yawVal = (envParams.yaw - 500) / 1000.0;
        double xp = cos(yawVal) * cos(pitchVal) * rawX + (cos(yawVal) * sin(pitchVal) * sin(rollVal) - sin(yawVal) * cos(rollVal)) * rawY + (cos(yawVal) * sin(pitchVal) * cos(rollVal) + sin(yawVal) * sin(rollVal)) * rawZ;
        double yp = sin(yawVal) * cos(pitchVal) * rawX + (sin(yawVal) * sin(pitchVal) * sin(rollVal) + cos(yawVal) * cos(rollVal)) * rawY + (sin(yawVal) * sin(pitchVal) * cos(rollVal) - cos(yawVal) * sin(rollVal)) * rawZ;
        double zp = -sin(pitchVal) * rawX + cos(pitchVal) * sin(rollVal) * rawY + cos(pitchVal) * cos(rollVal) * rawZ;
        double x = xp + (envParams.X - 500) / 100.0;
        double y = yp + (envParams.Y - 500) / 100.0;
        double z = zp + (envParams.Z - 500) / 100.0;

        if (z > 0)
        {
            int u = (int)(width / 2 + f_x * x / z);
            int v = (int)(height / 2 + f_x * y / z);
            if (0 <= u && u < width && 0 <= v && v < height)
            {
                auto it = lower_bound(tans.begin(), tans.end(), rawY / r);
                int index = it - tans.begin();
                all_layers[index].emplace_back(x, y, z);
            }
        }
    }

    for (int i = 0; i < 64; i++)
    {
        // no sort
        vector<Eigen::Vector3d> removed;
        for (size_t j = 0; j < all_layers[i].size(); j++)
        {
            while (removed.size() > 0 && removed.back()[0] * all_layers[i][j][2] >= all_layers[i][j][0] * removed.back()[2])
            {
                removed.pop_back();
            }
            removed.emplace_back(all_layers[i][j]);
        }
    }

    original_grid = vector<vector<double>>(64, vector<double>(width, -1));
    filtered_grid = vector<vector<double>>(layer_cnt, vector<double>(width, -1));
    original_interpolate_grid = vector<vector<double>>(64, vector<double>(width, -1));
    filtered_interpolate_grid = vector<vector<double>>(layer_cnt, vector<double>(width, -1));
    vs = vector<vector<int>>(64, vector<int>(width, -1));
    for (int i = 0; i < 64; i++)
    {
        if (all_layers[i].size() == 0)
        {
            continue;
        }

        int now = 0;
        int u0 = (int)(width / 2 + f_x * all_layers[i][0][0] / all_layers[i][0][2]);
        int v0 = (int)(height / 2 + f_x * all_layers[i][0][1] / all_layers[i][0][2]);
        while (now < u0)
        {
            original_interpolate_grid[i][now] = all_layers[i][0][2];
            vs[i][now] = v0;
            now++;
        }
        int uPrev = u0;
        int vPrev = v0;
        for (int j = 0; j + 1 < all_layers[i].size(); j++)
        {
            int u = (int)(width / 2 + f_x * all_layers[i][j + 1][0] / all_layers[i][j + 1][2]);
            int v = (int)(height / 2 + f_x * all_layers[i][j + 1][1] / all_layers[i][j + 1][2]);
            original_grid[i][u] = all_layers[i][j][2];

            while (now < min(width, u))
            {
                double z = all_layers[i][j][2] + (now - uPrev) * (all_layers[i][j + 1][2] - all_layers[i][j][2]) / (u - uPrev);
                original_interpolate_grid[i][now] = z;
                vs[i][now] = vPrev + (now - uPrev) * (v - vPrev) / (u - uPrev);
                now++;
            }
            uPrev = u;
        }

        int uLast = (int)(width / 2 + f_x * all_layers[i].back()[0] / all_layers[i].back()[2]);
        int vLast = (int)(height / 2 + f_x * all_layers[i].back()[1] / all_layers[i].back()[2]);
        original_grid[i][uLast] = all_layers[i].back()[2];
        while (now < width)
        {
            original_interpolate_grid[i][now] = all_layers[i].back()[2];
            vs[i][now] = vLast;
            now++;
        }
    }
    for (int i = 0; i < layer_cnt; i++)
    {
        for (int j = 0; j < width; j++)
        {
            filtered_grid[i][j] = original_grid[i * (64 / layer_cnt)][j];
            filtered_interpolate_grid[i][j] = original_interpolate_grid[i * (64 / layer_cnt)][j];
        }
    }

    { // Check
        auto original_ptr = make_shared<geometry::PointCloud>();
        auto filtered_ptr = make_shared<geometry::PointCloud>();
        for (int i = 0; i < 64; i++)
        {
            for (int j = 0; j < width; j++)
            {
                double z = original_grid[i][j];
                if (z < 0)
                {
                    continue;
                }
                double x = z * (j - width / 2) / f_x;
                double y = z * (vs[i][j] - height / 2) / f_x;
                original_ptr->points_.emplace_back(x, y, z);
            }

            if (i % (64 / layer_cnt) == 0)
            {
                for (int j = 0; j < width; j++)
                {
                    double z = filtered_interpolate_grid[i / (64 / layer_cnt)][j];
                    if (z < 0)
                    {
                        continue;
                    }
                    double x = z * (j - width / 2) / f_x;
                    double y = z * (vs[i][j] - height / 2) / f_x;
                    filtered_ptr->points_.emplace_back(x, y, z);
                }
            }
        }
        //visualization::DrawGeometries({original_ptr}, "Points", 1200, 720);
    }
}

double segmentate(int data_no, EnvParams envParams, double gaussian_sigma, double sigma_c = 1, double sigma_s = 15, double sigma_r = 20, int r = 10, bool see_res = false)
{
    const string img_name = envParams.folder_path + to_string(data_no) + ".png";
    const string file_name = envParams.folder_path + to_string(data_no) + ".pcd";
    const bool vertical = true;

    auto img = cv::imread(img_name);
    cv::Mat blured;
    cv::GaussianBlur(img, blured, cv::Size(3, 3), gaussian_sigma);

    int length = width * height;
    vector<cv::Vec3b> params_x(length);
    Eigen::VectorXd params_z(length);

    geometry::PointCloud pointcloud;
    auto pcd_ptr = make_shared<geometry::PointCloud>();
    if (!io::ReadPointCloud(file_name, pointcloud))
    {
        cout << "Cannot read" << endl;
    }

    auto start = chrono::system_clock::now();

    vector<vector<double>> original_grid, filtered_grid, original_interpolate_grid, filtered_interpolate_grid;
    vector<vector<int>> vs;
    *pcd_ptr = pointcloud;
    int layer_cnt = 16;
    calc_grid(pcd_ptr, envParams, original_grid, filtered_grid, original_interpolate_grid, filtered_interpolate_grid, vs, layer_cnt);

    /*
    shared_ptr<UnionFind> color_segments;
    {
        Graph graph(&blured);
        color_segments = graph.segmentate(color_segment_k, color_size_min);
    }
    */

    auto interpolated_ptr = make_shared<geometry::PointCloud>();
    vector<vector<double>> interpolated_z(64, vector<double>(width, 0));
    {
        // Linear interpolation
        for (int i = 0; i + 1 < layer_cnt; i++)
        {
            for (int j = 0; j < width; j++)
            {
                double delta = (filtered_interpolate_grid[i + 1][j] - filtered_interpolate_grid[i][j]) / (64 / layer_cnt);
                double z = filtered_interpolate_grid[i][j];
                for (int k = 0; k < 64 / layer_cnt; k++)
                {
                    interpolated_z[i * (64 / layer_cnt) + k][j] = z;
                    z += delta;
                }
            }
        }
    }

    // PWAS
    vector<vector<double>> credibilities(64, vector<double>(width, 0));
    {
        int dx[] = {1, -1, 0, 0};
        int dy[] = {0, 0, 1, -1};
        for (int i = 0; i < 64; i++)
        {
            for (int j = 0; j < width; j++)
            {
                double val = -4 * interpolated_z[i][j];
                for (int k = 0; k < 4; k++)
                {
                    int x = j + dx[k];
                    int y = i + dy[k];
                    if (x < 0 || x >= width || y < 0 || y >= 64)
                    {
                        continue;
                    }

                    val += interpolated_z[y][x];
                }
                credibilities[i][j] = exp(-val * val / 2 / sigma_c / sigma_c);
            }
        }
    }

    // Still slow
    {
        int quantize_cnt = 8;
        vector<vector<vector<double>>> line_numerator(64, vector<vector<double>>(width, vector<double>(quantize_cnt + 1, 0)));
        vector<vector<vector<double>>> line_denominator(64, vector<vector<double>>(width, vector<double>(quantize_cnt + 1, 0)));
        for (int i = 0; i < 64; i++)
        {
            for (int j = 0; j < width; j++)
            {
                for (int k = 0; k <= quantize_cnt; k++)
                {
                    double numerator = 0;
                    double denominator = 0;
                    uchar d0 = (uchar)(256 * k / quantize_cnt);
                    for (int l = 0; l < r; l++)
                    {
                        int dx = l - r / 2;
                        if (j + dx < 0 || j + dx >= width)
                        {
                            continue;
                        }

                        int v = vs[i][j + dx];
                        uchar d1 = blured.at<cv::Vec3b>(v, j + dx)[0];
                        double val = exp(-dx * dx / 2 / sigma_s / sigma_s) * exp(-(d0 - d1) * (d0 - d1) / 2 / sigma_r / sigma_r) * credibilities[i][j + dx];
                        numerator += val * interpolated_z[i][j + dx];
                        denominator += val;
                    }
                    line_numerator[i][j][k] = numerator;
                    line_denominator[i][j][k] = denominator;
                }
            }
        }

        for (int i = 0; i < 64; i++)
        {
            for (int j = 0; j < width; j++)
            {
                double coef = 0;
                double val = 0;
                int v = vs[i][j];
                int d0 = blured.at<cv::Vec3b>(v, j)[0];
                int k = d0 / (256 / quantize_cnt);
                int remain = d0 - k * (256 / quantize_cnt);
                for (int ii = 0; ii < r; ii++)
                {
                    int dy = ii - r / 2;
                    if (i + dy < 0 || i + dy >= 64)
                    {
                        continue;
                    }

                    val += exp(-dy * dy / 2 / sigma_s / sigma_s) * (line_numerator[i + dy][j][k] + remain * (line_numerator[i + dy][j][k + 1] - line_numerator[i + dy][j][k]) / (256 / quantize_cnt));
                    coef += exp(-dy * dy / 2 / sigma_s / sigma_s) * (line_denominator[i + dy][j][k] + remain * (line_denominator[i + dy][j][k + 1] - line_denominator[i + dy][j][k]) / (256 / quantize_cnt));
                }
                interpolated_z[i][j] = val / coef;
            }
        }
        // cv::imshow("c", jbu_img);
    }

    {
        for (int i = 0; i < 64; i++)
        {
            for (int j = 0; j < width; j++)
            {
                double z = interpolated_z[i][j];
                double tanVal = (i - height / 2) / f_x;
                if (original_grid[i][j] <= 0 || z <= 0 /*z < 0 || original_grid[i][j] == 0*/)
                {
                    continue;
                }

                double x = z * (j - width / 2) / f_x;
                double y = z * (vs[i][j] - height / 2) / f_x;

                double color = blured.at<cv::Vec3b>(vs[i][j], j)[0] / 255.0;
                interpolated_ptr->points_.emplace_back(x, y, z);
                interpolated_ptr->colors_.emplace_back(color, color, color);
            }
        }
    }

    double error = 0;
    { // Evaluation
        int cnt = 0;
        int cannot_cnt = 0;
        for (int i = 0; i < 64; i++)
        {
            if (i % (64 / layer_cnt) == 0)
            {
                continue;
            }

            for (int j = 0; j < width; j++)
            {
                if (original_grid[i][j] > 0 && interpolated_z[i][j] > 0)
                {
                    error += abs((original_grid[i][j] - interpolated_z[i][j]) / original_grid[i][j]);
                    cnt++;
                }
            }
        }
        error /= cnt;
        //cout << "cannot cnt = " << (64 - layer_cnt) * width - cnt << endl;
        cout << "Error = " << error << endl;
    }
    cout << chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now() - start).count() << "ms" << endl;
    ofs << data_no << "," << chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now() - start).count() << "," << error << "," << endl;

    if (see_res)
    {
        Eigen::MatrixXd front(4, 4);
        front << 1, 0, 0, 0,
            0, -1, 0, 0,
            0, 0, -1, 0,
            0, 0, 0, 1;
        interpolated_ptr->Transform(front);
        visualization::DrawGeometries({interpolated_ptr}, "a", 1600, 900);
    }

    return error;
}

int main(int argc, char *argv[])
{
    //vector<int> data_nos = {550, 1000, 1125, 1260, 1550}; // 03_03_miyanosawa
    //vector<int> data_nos = {10, 20, 30, 40, 50}; // 02_04_13jo
    //vector<int> data_nos = {700, 1290, 1460, 2350, 3850}; // 02_04_miyanosawa

    vector<int> data_nos;
    for (int i = 1100; i < 1300; i++)
    {
        data_nos.emplace_back(i);
    }

    // Calibration
    // 03_03_miyanosawa
    /*
int X = 500;
int Y = 474;
int Z = 458;
int theta = 506;
int phi = 527;
*/

    EnvParams params_13jo = {498, 485, 509, 481, 517, 500, "../../../data/2020_02_04_13jo/", {10, 20, 30, 40, 50}, "res_linear_13jo.csv"};
    EnvParams params_miyanosawa = {495, 475, 458, 488, 568, 500, "../../../data/2020_02_04_miyanosawa/", {700, 1290, 1460, 2350, 3850}, "res_linear_miyanosawa.csv"};
    EnvParams params_miyanosawa2 = {495, 475, 458, 488, 568, 500, "../../../data/2020_02_04_miyanosawa/", data_nos, "res_linear_miyanosawa_1100-1300.csv"};

    EnvParams params_use = params_miyanosawa;
    ofs = ofstream(params_use.of_name);

    for (int i = 0; i < params_use.data_ids.size(); i++)
    {
        segmentate(params_use.data_ids[i], params_use, 0.5, 1, 46, 1, 19, false);
    }
    double best_error = 1000;
    double best_sigma_c = 1;
    double best_sigma_s = 1;
    double best_sigma_r = 1;
    int best_r = 1;
    // best params 2020/07/06 sigma_c:91 sigma_s:46 sigma_R:1 r:19

    for (double sigma_c = 10; sigma_c < 1000; sigma_c += 10)
    {
        for (double sigma_s = 1; sigma_s < 50; sigma_s += 5)
        {
            for (double sigma_r = 1; sigma_r < 10; sigma_r += 1)
            {
                for (int r = 1; r < 10; r += 2)
                {
                    double error = 0;
                    for (int i = 0; i < params_use.data_ids.size(); i++)
                    {
                        error += segmentate(params_use.data_ids[i], params_use, 0.5, sigma_c, sigma_s, sigma_r, r, false);
                    }

                    if (best_error > error)
                    {
                        best_error = error;
                        best_sigma_c = sigma_c;
                        best_sigma_s = sigma_s;
                        best_sigma_r = sigma_r;
                        best_r = r;
                    }
                }
            }
        }
    }

    cout << "Sigma C = " << best_sigma_c << endl;
    cout << "Sigma S = " << best_sigma_s << endl;
    cout << "Sigma R = " << best_sigma_r << endl;
    cout << "R = " << best_r << endl;
    cout << "Mean error = " << best_error / data_nos.size() << endl;
    return 0;
}