#include "deformationGraph.h"

DeformationGraph::DeformationGraph()
{

}

void DeformationGraph::Init(_GLMmodel* originMesh, _GLMmodel* samplingMesh)
{
	this->originMesh = originMesh;
    this->samplingMesh = samplingMesh;

    k_nearest = 8;
    w_rot = 1.0f;
    w_reg = 10.0f;
    w_con = 100.0f;

    sample_controls = 0;
    sample_nodes = samplingMesh->numvertices;

    InitRotAndTrans();
    CalConnectedMap();
    CalSamplingVertices();
    CalEmbeddedWeights();
    CalDeformationGraphWeights();
    
    default_sampling_vertices = vector<Vector3f>(sample_nodes);
    for (int i = 0; i < sample_nodes; ++i)
    {
        default_sampling_vertices[i] = Vector3f(
            samplingMesh->vertices[(i + 1) * 3 + 0],
            samplingMesh->vertices[(i + 1) * 3 + 1],
            samplingMesh->vertices[(i + 1) * 3 + 2]);
    }

    default_origin_vertices = vector<Vector3f>(originMesh->numvertices);
    for (int i = 0; i < originMesh->numvertices; ++i)
    {
        default_origin_vertices[i] = Vector3f(
            originMesh->vertices[(i + 1) * 3 + 0],
            originMesh->vertices[(i + 1) * 3 + 1],
            originMesh->vertices[(i + 1) * 3 + 2]);
    }

    for (auto iter = connectedMap.begin(); iter != connectedMap.end(); ++iter)
    {
        sample_edges += (*iter).second.size();
    }
    sample_edges /= 2;
}

void DeformationGraph::InitRotAndTrans()
{
    rot.resize(sample_nodes);
    trans.resize(sample_nodes);
    
    Matrix3f temp_r = Matrix3f::Zero();
    Vector3f temp_t = Vector3f::Zero();
    
    temp_r(0, 0) = temp_r(1, 1) = temp_r(2, 2) = 1.0f;

    for (int i = 0; i < sample_nodes; ++i)
    {
        rot[i] = temp_r;
        trans[i] = temp_t;
    }
}

void DeformationGraph::CalSamplingVertices()
{
    for (int i = 1; i <= samplingMesh->numvertices; ++i)
    {
        float min = INT_MAX;
        int idx = 0;
        for (int j = 1; j <= originMesh->numvertices; ++j)
        {
            float temp = fabs(samplingMesh->vertices[i * 3 + 0] - originMesh->vertices[j * 3 + 0])
                        + fabs(samplingMesh->vertices[i * 3 + 1] - originMesh->vertices[j * 3 + 1])
                        + fabs(samplingMesh->vertices[i * 3 + 2] - originMesh->vertices[j * 3 + 2]);
            if (temp < min)
            {
                min = temp;
                idx = j;
            }
        }
        sample_idices.push_back(idx);
        samplingMesh->vertices[i * 3 + 0] = originMesh->vertices[idx * 3 + 0];
        samplingMesh->vertices[i * 3 + 1] = originMesh->vertices[idx * 3 + 1];
        samplingMesh->vertices[i * 3 + 2] = originMesh->vertices[idx * 3 + 2];
    }

    cout << "Fit Sample Vertices:" << sample_idices.size() << endl;
}

void DeformationGraph::CalConnectedMap()
{
    for (int i = 0; i < samplingMesh->numtriangles; ++i)
    {
        for (int j = 0; j < 3; ++j)
        {
            connectedMap.insert(pair<int, set<int>>(samplingMesh->triangles[i].vindices[j], set<int>()));

            connectedMap[samplingMesh->triangles[i].vindices[j]].insert(samplingMesh->triangles[i].vindices[(j + 1) % 3]);
            connectedMap[samplingMesh->triangles[i].vindices[j]].insert(samplingMesh->triangles[i].vindices[(j + 2) % 3]);
        }
    }
}

void DeformationGraph::Run()
{
    UpdateSampleVertices();

    GaussainNewton();
}

void DeformationGraph::UpdateDeformationGraph()
{
    for (int i = 0; i < sample_nodes; ++i)
    {
        samplingMesh->vertices[(i + 1) * 3 + 0] += trans[i][0];
        samplingMesh->vertices[(i + 1) * 3 + 1] += trans[i][1];
        samplingMesh->vertices[(i + 1) * 3 + 2] += trans[i][2];
    }
}

void DeformationGraph::UpdateSampleVertices()
{
    for (int i = 0; i < sample_nodes; ++i)
    {
        samplingMesh->vertices[(i + 1) * 3 + 0] = default_sampling_vertices[i][0];
        samplingMesh->vertices[(i + 1) * 3 + 1] = default_sampling_vertices[i][1];
        samplingMesh->vertices[(i + 1) * 3 + 2] = default_sampling_vertices[i][2];
    }

    for (int i = 0; i < originMesh->numvertices; ++i)
    {
        originMesh->vertices[(i + 1) * 3 + 0] = default_origin_vertices[i][0];
        originMesh->vertices[(i + 1) * 3 + 1] = default_origin_vertices[i][1];
        originMesh->vertices[(i + 1) * 3 + 2] = default_origin_vertices[i][2];
    }
}

void DeformationGraph::UpdateOriginMesh()
{
    int nodes = originMesh->numvertices;
    vector<Vector3f> results(nodes);
    for (int i = 0; i < nodes; ++i)
    {
        Vector3f result(0, 0, 0);
        Vector3f vi = Vector3f(
            originMesh->vertices[(i + 1) * 3 + 0],
            originMesh->vertices[(i + 1) * 3 + 1],
            originMesh->vertices[(i + 1) * 3 + 2]);

        for (int j = 0; j < k_nearest; ++j)
        {
            Vector3f gj = Vector3f(
                samplingMesh->vertices[(embeddedWeights[i][j].first) * 3 + 0],
                samplingMesh->vertices[(embeddedWeights[i][j].first) * 3 + 1],
                samplingMesh->vertices[(embeddedWeights[i][j].first) * 3 + 2]);

            result += (embeddedWeights[i][j].second) * (rot[embeddedWeights[i][j].first - 1] * (vi - gj) + gj + trans[embeddedWeights[i][j].first - 1]);
        }
        results[i] = result;
    }

    for (int i = 0; i < nodes; ++i)
    {
        originMesh->vertices[(i + 1) * 3 + 0] = results[i][0];
        originMesh->vertices[(i + 1) * 3 + 1] = results[i][1];
        originMesh->vertices[(i + 1) * 3 + 2] = results[i][2];
    }
}

void DeformationGraph::GaussainNewton()
{
    const float epsilon = 1e-3;
    const int iter_max = 5;
    float err_current = 1, err_past = 2;
    SparseMatrix<float> J(6 * sample_nodes + 6 * sample_edges + 3 * sample_controls, 12 * sample_nodes);
    MatrixXf f, h, x(12 * sample_nodes, 1);
    
    InitRotAndTrans();

    for (int i = 0; i < sample_nodes; ++i)
    {
        for (int j = 0; j < 9; ++j) x(i * 12 + j, 0) = rot[i](j / 3, j % 3);
        for (int j = 0; j < 3; ++j) x(i * 12 + 9 + j, 0) = trans[i](j);
    }
    
    cout << "------------------- Start -------------------" << endl;
    
    err_current = F(x);
    for (int i = 0; i < iter_max && fabs(err_past - err_current) > epsilon && err_current > epsilon; ++i)
    {
        err_past = err_current;

        Calf(f);
        CalJ(J);

        SparseMatrix<float> Jt = J.transpose();
        SparseMatrix<float> JtJ = J.transpose() * J;

        SimplicialCholesky<SparseMatrix<float>> solver(Jt * J);
        MatrixXf h = solver.solve(Jt * f);

        x = h;

        cout << "---------------------------------------\n";
        cout << "Iteration: " << i + 1<< endl;
        err_current = F(x);
        cout << "Error: " << err_current << endl;
    }

    cout << "------------------- Finished -------------------\n" << endl;
}

void DeformationGraph::Calf(MatrixXf &f)
{
    int idx = 0;
    MatrixXf fx = MatrixXf::Zero(6 * sample_nodes + (3 * sample_edges) * 2 + 3 * sample_controls, 1);

    // Erot
    for (int i = 0; i < sample_nodes; ++i)
    {
        fx(idx++, 0) = rot[i].col(0).dot(rot[i].col(1)) * sqrt(w_rot);
        fx(idx++, 0) = rot[i].col(0).dot(rot[i].col(2)) * sqrt(w_rot);
        fx(idx++, 0) = rot[i].col(1).dot(rot[i].col(2)) * sqrt(w_rot);
        fx(idx++, 0) = sqrt(w_rot);
        fx(idx++, 0) = sqrt(w_rot);
        fx(idx++, 0) = sqrt(w_rot);
    }

    // Ereg
    for (int j = 0; j < sample_nodes; ++j)
    {
        Vector3f gj = Vector3f(
            samplingMesh->vertices[(j + 1) * 3 + 0],
            samplingMesh->vertices[(j + 1) * 3 + 1],
            samplingMesh->vertices[(j + 1) * 3 + 2]);
        for (auto iter = connectedMap[j + 1].begin(); iter != connectedMap[j + 1].end(); ++iter)
        {
            int k = *iter - 1;
            Vector3f gk = Vector3f(
                samplingMesh->vertices[(k + 1) * 3 + 0],
                samplingMesh->vertices[(k + 1) * 3 + 1],
                samplingMesh->vertices[(k + 1) * 3 + 2]);

            Vector3f temp = (gk - gj) * sqrt(w_reg);
            
            fx(idx++, 0) = temp(0);
            fx(idx++, 0) = temp(1);
            fx(idx++, 0) = temp(2);
        }
    }

    // Econ
    for (int i = 0; i < control_points_data.size(); ++i)
    {
        for (int j = 0; j < control_points_data[i].size(); ++j)
        {
            Vector3f temp = Vector3f(
                control_points_data[i][j][0] - samplingMesh->vertices[control_points_id[i][j] * 3 + 0],
                control_points_data[i][j][1] - samplingMesh->vertices[control_points_id[i][j] * 3 + 1],
                control_points_data[i][j][2] - samplingMesh->vertices[control_points_id[i][j] * 3 + 2]) * sqrt(w_con);
            
            fx(idx++, 0) = temp(0);
            fx(idx++, 0) = temp(1);
            fx(idx++, 0) = temp(2);
        }
    }

    f = fx;
}

void DeformationGraph::CalJ(SparseMatrix<float>& J)
{
    int idx = 0;
    vector<Triplet<float>> Jacobi;

    // Erot
    for (int i = 0; i < sample_nodes; ++i)
    {
        Jacobi.push_back(Triplet<float>(idx, 0 + 12 * i, rot[i](0, 1)));
        Jacobi.push_back(Triplet<float>(idx, 1 + 12 * i, rot[i](1, 1)));
        Jacobi.push_back(Triplet<float>(idx, 2 + 12 * i, rot[i](2, 1)));
        
        Jacobi.push_back(Triplet<float>(idx, 3 + 12 * i, rot[i](0, 0)));
        Jacobi.push_back(Triplet<float>(idx, 4 + 12 * i, rot[i](1, 0)));
        Jacobi.push_back(Triplet<float>(idx, 5 + 12 * i, rot[i](2, 0)));

        idx++;

        Jacobi.push_back(Triplet<float>(idx, 0 + 12 * i, rot[i](0, 2)));
        Jacobi.push_back(Triplet<float>(idx, 1 + 12 * i, rot[i](1, 2)));
        Jacobi.push_back(Triplet<float>(idx, 2 + 12 * i, rot[i](2, 2)));
        
        Jacobi.push_back(Triplet<float>(idx, 6 + 12 * i, rot[i](0, 0)));
        Jacobi.push_back(Triplet<float>(idx, 7 + 12 * i, rot[i](1, 0)));
        Jacobi.push_back(Triplet<float>(idx, 8 + 12 * i, rot[i](2, 0)));

        idx++;

        Jacobi.push_back(Triplet<float>(idx, 3 + 12 * i, rot[i](0, 2)));
        Jacobi.push_back(Triplet<float>(idx, 4 + 12 * i, rot[i](1, 2)));
        Jacobi.push_back(Triplet<float>(idx, 5 + 12 * i, rot[i](2, 2)));
        
        Jacobi.push_back(Triplet<float>(idx, 6 + 12 * i, rot[i](0, 1)));
        Jacobi.push_back(Triplet<float>(idx, 7 + 12 * i, rot[i](1, 1)));
        Jacobi.push_back(Triplet<float>(idx, 8 + 12 * i, rot[i](2, 1)));

        idx++;

        for (int j = 0; j < 9; ++j)
        {
            if (j == 3 || j == 6) ++idx;
            Jacobi.push_back(Triplet<float>(idx, 12 * i + j, rot[i](j / 3, j % 3)));
        }
        idx++;
    }
    
    // Ereg
    for (int j = 0; j < sample_nodes; ++j)
    {
        for (auto iter = connectedMap[j + 1].begin(); iter != connectedMap[j + 1].end(); ++iter)
        {
            int k = *iter - 1;

            vector3 ekj = vector3(
                samplingMesh->vertices[(k + 1) * 3 + 0] - samplingMesh->vertices[(j + 1) * 3 + 0],
                samplingMesh->vertices[(k + 1) * 3 + 1] - samplingMesh->vertices[(j + 1) * 3 + 1],
                samplingMesh->vertices[(k + 1) * 3 + 2] - samplingMesh->vertices[(j + 1) * 3 + 2]) * sqrt(w_reg);

            for (int p = 0; p < 3; ++p)
            {
                for (int q = 0; q < 3; ++q)
                {
                    Jacobi.push_back(Triplet<float>(idx, 12 * j + p * 3 + q, ekj[q]));
                }

                Jacobi.push_back(Triplet<float>(idx, 12 * j + 9 + p, sqrt(w_reg)));
                Jacobi.push_back(Triplet<float>(idx, 12 * k + 9 + p, -sqrt(w_reg)));

                idx++;
            }
        }
    }

    // Econ
    for (int i = 0; i < control_points_id.size(); ++i)
    {
        for (int j = 0; j < control_points_id[i].size(); ++j)
        {
            Jacobi.push_back(Triplet<float>(idx++, 9 + 12 * (control_points_id[i][j] - 1) + 0, sqrt(w_con)));
            Jacobi.push_back(Triplet<float>(idx++, 9 + 12 * (control_points_id[i][j] - 1) + 1, sqrt(w_con)));
            Jacobi.push_back(Triplet<float>(idx++, 9 + 12 * (control_points_id[i][j] - 1) + 2, sqrt(w_con)));
        }
    }

    J.setFromTriplets(Jacobi.begin(), Jacobi.end());
}

float DeformationGraph::F(MatrixXf &x)
{
    float err = 0;

    for (int i = 0; i < sample_nodes; ++i)
    {
        for (int j = 0; j < 9; ++j) rot[i](j / 3, j % 3) = x(12 * i + j, 0);
        for (int j = 0; j < 3; ++j) trans[i][j] = x(12 * i + 9 + j, 0);
    }

    UpdateOriginMesh();
    UpdateDeformationGraph();

    float Erot = CalErot(), Ereg = CalEreg(), Econ = CalEcon();

    cout << "Erot: " << Erot << ", Ereg: " << Ereg << ", Econ: " << Econ << endl;

    err = w_rot * Erot + w_reg * Ereg + w_con * Econ;

    return err;
}

float DeformationGraph::CalEreg()
{
    float Ereg = 0;

    for (int j = 0; j < sample_nodes; ++j)
    {
        Vector3f gj = Vector3f(
            samplingMesh->vertices[(j + 1) * 3 + 0],
            samplingMesh->vertices[(j + 1) * 3 + 1],
            samplingMesh->vertices[(j + 1) * 3 + 2]);

        for (auto iter = connectedMap[j + 1].begin(); iter != connectedMap[j + 1].end(); ++iter)
        {
            int k = *iter - 1;

            Vector3f gk = Vector3f(
                samplingMesh->vertices[(k + 1) * 3 + 0],
                samplingMesh->vertices[(k + 1) * 3 + 1],
                samplingMesh->vertices[(k + 1) * 3 + 2]);
            
            Ereg += pow((rot[j] * (gk - gj) + gj + trans[j] - (gk + trans[k])).norm(), 2);
        }
    }

    return Ereg;
}

float DeformationGraph::CalErot()
{
    float Erot = 0;

    for (int i = 0; i < sample_nodes; ++i)
    {
        Erot += pow(rot[i].col(0).dot(rot[i].col(1)), 2);
        Erot += pow(rot[i].col(0).dot(rot[i].col(2)), 2);
        Erot += pow(rot[i].col(1).dot(rot[i].col(2)), 2);
        Erot += pow(rot[i].col(0).dot(rot[i].col(0)) - 1, 2);
        Erot += pow(rot[i].col(1).dot(rot[i].col(1)) - 1, 2);
        Erot += pow(rot[i].col(2).dot(rot[i].col(2)) - 1, 2);
    }

    return Erot;
}

float DeformationGraph::CalEcon()
{
    float Econ = 0;

    for (int i = 0; i < control_points_id.size(); ++i)
    {
        for (int j = 0; j < control_points_id[i].size(); ++j)
        {
            int idx = control_points_id[i][j];
            Econ += pow(fabs(samplingMesh->vertices[idx * 3 + 0] - control_points_data[i][j][0]), 2);
            Econ += pow(fabs(samplingMesh->vertices[idx * 3 + 1] - control_points_data[i][j][1]), 2);
            Econ += pow(fabs(samplingMesh->vertices[idx * 3 + 2] - control_points_data[i][j][2]), 2);
        }
    }

    return Econ;
}

void DeformationGraph::CalDeformationGraphWeights()
{
    vector<vector<float>> distance(sample_nodes);
    vector<vector<int>> index(sample_nodes);

    for (int i = 0; i < sample_nodes; ++i)
    {
        vector<float> temp_d(sample_nodes);
        vector<int> temp_i(sample_nodes);
        vector3 temp_v;
        for (int j = 0; j < sample_nodes; ++j)
        {
            temp_v = vector3(
                samplingMesh->vertices[(i + 1) * 3 + 0] - samplingMesh->vertices[(j + 1) * 3 + 0],
                samplingMesh->vertices[(i + 1) * 3 + 1] - samplingMesh->vertices[(j + 1) * 3 + 1],
                samplingMesh->vertices[(i + 1) * 3 + 2] - samplingMesh->vertices[(j + 1) * 3 + 2]);
            temp_d[j] = sqrt(temp_v[0] * temp_v[0] + temp_v[1] * temp_v[1] + temp_v[2] * temp_v[2]);
            temp_i[j] = j;
        }
        distance[i] = temp_d;
        index[i] = temp_i;
    }

    deformationGraphWeights = vector<vector<pair<int, float>>>(sample_nodes);
    for (int i = 0; i < sample_nodes; ++i)
    {
        vector<pair<int, float>> temp_p(k_nearest + 1);
        for (int j = 0; j < k_nearest + 1; ++j)
        {
            int idx_min = j;

            for (int k = j + 1; k < sample_nodes; ++k)
            {
                if (distance[i][k] < distance[i][idx_min]) idx_min = k;
            }

            float temp = distance[i][j];
            distance[i][j] = distance[i][idx_min];
            distance[i][idx_min] = temp;

            int temp_i = index[i][j];
            index[i][j] = index[i][idx_min];
            index[i][idx_min] = temp_i;

            temp_p[j] = pair<int, float>(index[i][j] + 1, distance[i][j]);
        }
        deformationGraphWeights[i] = temp_p;
    }

    for (int i = 0; i < sample_nodes; ++i)
    {
        vector3 temp = vector3(
            samplingMesh->vertices[(i + 1) * 3 + 0] - samplingMesh->vertices[deformationGraphWeights[i][k_nearest].first * 3 + 0],
            samplingMesh->vertices[(i + 1) * 3 + 1] - samplingMesh->vertices[deformationGraphWeights[i][k_nearest].first * 3 + 1],
            samplingMesh->vertices[(i + 1) * 3 + 2] - samplingMesh->vertices[deformationGraphWeights[i][k_nearest].first * 3 + 2]);
        float d_max = sqrt(temp[0] * temp[0] + temp[1] * temp[1] + temp[2] * temp[2]);
        float sum = 0;

        for (int j = 0; j < k_nearest; ++j)
        {
            temp = vector3(
                samplingMesh->vertices[(i + 1) * 3 + 0] - samplingMesh->vertices[deformationGraphWeights[i][j].first * 3 + 0],
                samplingMesh->vertices[(i + 1) * 3 + 1] - samplingMesh->vertices[deformationGraphWeights[i][j].first * 3 + 1],
                samplingMesh->vertices[(i + 1) * 3 + 2] - samplingMesh->vertices[deformationGraphWeights[i][j].first * 3 + 2]);
            float d = sqrt(temp[0] * temp[0] + temp[1] * temp[1] + temp[2] * temp[2]);

            deformationGraphWeights[i][j].second = pow(1 - d / d_max, 2);
            sum += deformationGraphWeights[i][j].second;
        }

        for (int j = 0; j < k_nearest; ++j)
        {
            deformationGraphWeights[i][j].second /= sum;
            if (k_nearest == 1) deformationGraphWeights[i][j].second = 1;
        }
    }
}

void DeformationGraph::CalEmbeddedWeights()
{
    int nodes = originMesh->numvertices;
    vector<vector<float>> distance(nodes);
    vector<vector<int>> index(nodes);

    for (int i = 0; i < nodes; ++i)
    {
        vector<float> temp_d(sample_nodes);
        vector<int> temp_i(sample_nodes);
        vector3 temp_v;
        for (int j = 0; j < sample_nodes; ++j)
        {
            temp_v = vector3(
                originMesh->vertices[(i + 1) * 3 + 0] - samplingMesh->vertices[(j + 1) * 3 + 0],
                originMesh->vertices[(i + 1) * 3 + 1] - samplingMesh->vertices[(j + 1) * 3 + 1],
                originMesh->vertices[(i + 1) * 3 + 2] - samplingMesh->vertices[(j + 1) * 3 + 2]);
            temp_d[j] = sqrt(temp_v[0] * temp_v[0] + temp_v[1] * temp_v[1] + temp_v[2] * temp_v[2]);
            temp_i[j] = j;
        }
        distance[i] = temp_d;
        index[i] = temp_i;
    }
    
    embeddedWeights = vector<vector<pair<int, float>>>(nodes);
    for (int i = 0; i < nodes; ++i)
    {
        vector<pair<int, float>> temp_p(k_nearest + 1);
        for (int j = 0; j < k_nearest + 1; ++j)
        {
            int idx_min = j;

            for (int k = j + 1; k < sample_nodes; ++k)
            {
                if (distance[i][k] < distance[i][idx_min]) idx_min = k;
            }

            float temp = distance[i][j];
            distance[i][j] = distance[i][idx_min];
            distance[i][idx_min] = temp;

            int temp_i = index[i][j];
            index[i][j] = index[i][idx_min];
            index[i][idx_min] = temp_i;

            temp_p[j] = pair<int, float>(index[i][j] + 1, distance[i][j]);
        }
        embeddedWeights[i] = temp_p;
    }
    
    for (int i = 0; i < nodes; ++i)
    {
        vector3 temp = vector3(
            originMesh->vertices[(i + 1) * 3 + 0] - samplingMesh->vertices[embeddedWeights[i][k_nearest].first * 3 + 0],
            originMesh->vertices[(i + 1) * 3 + 1] - samplingMesh->vertices[embeddedWeights[i][k_nearest].first * 3 + 1],
            originMesh->vertices[(i + 1) * 3 + 2] - samplingMesh->vertices[embeddedWeights[i][k_nearest].first * 3 + 2]);
        float d_max = sqrt(temp[0] * temp[0] + temp[1] * temp[1] + temp[2] * temp[2]);
        float sum = 0;

        for (int j = 0; j < k_nearest; ++j)
        {
            temp = vector3(
                originMesh->vertices[(i + 1) * 3 + 0] - samplingMesh->vertices[embeddedWeights[i][j].first * 3 + 0],
                originMesh->vertices[(i + 1) * 3 + 1] - samplingMesh->vertices[embeddedWeights[i][j].first * 3 + 1],
                originMesh->vertices[(i + 1) * 3 + 2] - samplingMesh->vertices[embeddedWeights[i][j].first * 3 + 2]);
            float d = sqrt(temp[0] * temp[0] + temp[1] * temp[1] + temp[2] * temp[2]);

            embeddedWeights[i][j].second = pow(1 - d / d_max, 2);
            sum += embeddedWeights[i][j].second;
        }

        for (int j = 0; j < k_nearest; ++j)
        {
            embeddedWeights[i][j].second /= sum;
            if (k_nearest == 1) embeddedWeights[i][j].second = 1;
        }
    }
}

void DeformationGraph::SetControlPoints(vector<vector<int>> controlPoints)
{
    control_points_id = controlPoints;

    for (int i = control_points_data.size(); i < control_points_id.size(); ++i)
    {
        vector<vector3> temp_p;
        vector3 temp_v;
        for (int j = 0; j < control_points_id[i].size(); ++j)
        {
            temp_v = vector3(
                samplingMesh->vertices[control_points_id[i][j] * 3 + 0],
                samplingMesh->vertices[control_points_id[i][j] * 3 + 1],
                samplingMesh->vertices[control_points_id[i][j] * 3 + 2]);
            temp_p.push_back(temp_v);
        }
        control_points_data.push_back(temp_p);
    }

    sample_controls = 0;
    for (int i = 0; i < controlPoints.size(); ++i)
    {
        sample_controls += controlPoints[i].size();
    }
}

void DeformationGraph::SetControlPointsTranslate(int selectedId, vector3 vec)
{
    for (int i = 0; i < control_points_id[selectedId].size(); i++)
    {
        int idx = control_points_id[selectedId][i];

        control_points_data[selectedId][i] += vec;
    }
}

vector<int> DeformationGraph::GetSamplingIndices()
{
    return sample_idices;
}