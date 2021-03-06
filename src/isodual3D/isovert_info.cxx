/// \file isovert_info.cxx
/// Print isovert information
/// Version 0.1.0

/*
  IJK: Isosurface Jeneration Kode
  Copyright (C) 2012-2013 Rephael Wenger

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public License
  (LGPL) as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/


#include <iostream>
#include <iomanip>

#include "ijkcoord.txx"
#include "ijkmesh.txx"
#include "ijkmesh_geom.txx"
#include "ijkprint.txx"
#include "ijkstring.txx"

#include "sharpiso_scalar.txx"
#include "isodual3D.h"
#include "isodual3DIO.h"
#include "isodual3D_decimate.h"
#include "isodual3D_extract.h"
#include "isodual3D_isovert.h"
#include "isodual3D_position.h"
#include "ijkdualtable.h"

using namespace IJK;
using namespace ISODUAL3D;

using namespace std;

// global variables
std::vector<VERTEX_INDEX> input_cube_coord;
VERTEX_INDEX input_cube_index;
bool flag_input_cube(false);
bool flag_list_gradients(false);
bool flag_list_normalized_gradients(false);
bool flag_list_iso_points(false);
bool flag_disk_info(false);

// local subroutines
void memory_exhaustion();
void print_isovert_info
(const INPUT_INFO & input_info, const ISODUAL_DATA & isodual_data);
void compute_eigenvalues
(const SCALAR_TYPE isovalue, 
 const ISODUAL_DATA & isodual_data, const GRID_CUBE & gcube,
 const SHARPISO_EDGE_INDEX_GRID & edge_index,
 EIGENVALUE_TYPE eigenvalues[DIM3], 
 EIGENVALUE_TYPE normalized_eigenvalues[DIM3],
 NUM_TYPE num_large_eigenvalues);
void insert_selected_in_bin_grid
(const ISOVERT & isovert, const AXIS_SIZE_TYPE bin_width,
 BIN_GRID<VERTEX_INDEX> & bin_grid);
void out_gcube(std::ostream & out, const SCALAR_TYPE isovalue,
               const ISODUAL_DATA & isodual_data, const GRID_CUBE & gcube,
               const SHARPISO_EDGE_INDEX_GRID & edge_index);
void out_gradients
(std::ostream & out, const SHARPISO_SCALAR_GRID_BASE & scalar_grid,
 const GRADIENT_GRID_BASE & gradient_grid, const VERTEX_INDEX cube_index,
 const SCALAR_TYPE isovalue, const ISODUAL_PARAM & param);
void out_gcube_multi
(std::ostream & out, const SCALAR_TYPE isovalue, 
 const ISODUAL_DATA & isodual_data,
 const IJKDUALTABLE::ISODUAL_CUBE_TABLE & isodual_table,
 const ISOVERT & isovert,
 const NUM_TYPE gcube_index,
 const SHARPISO_EDGE_INDEX_GRID & edge_index,
 const std::vector<ISODUAL3D::CUBE_ISOVERT_DATA> & cube_isovert_data,
 const std::vector<VERTEX_INDEX> & gcube_map,
 const BIN_GRID<VERTEX_INDEX> & bin_grid);
void out_gcube
(std::ostream & out, const SCALAR_TYPE isovalue, 
 const ISODUAL_DATA & isodual_data,
 const ISOVERT & isovert,
 const VERTEX_INDEX gcube_index,
 const SHARPISO_EDGE_INDEX_GRID & edge_index,
 const std::vector<VERTEX_INDEX> & gcube_map,
 const BIN_GRID<VERTEX_INDEX> & bin_grid);
void out_gcube_type(std::ostream & out, const GRID_CUBE_FLAG flag);
void out_neighborhood
(std::ostream & out, const SHARPISO_SCALAR_GRID_BASE & scalar_grid,
 const VERTEX_INDEX cube_index,
 const ISOVERT & isovert, const std::vector<VERTEX_INDEX> & gcube_map);
void parse_isovert_command_line
(int argc, char **argv, INPUT_INFO & input_info);

// **************************************************
// MAIN
// **************************************************

int main(int argc, char **argv)
{
  time_t start_time;
  //time(&start_time);  // debug

  ISODUAL_TIME isodual_time;
  IO_TIME io_time = {0.0, 0.0, 0.0};
  INPUT_INFO input_info;
  IJK::ERROR error;
  bool flag_gradient(false);
  try {

    std::set_new_handler(memory_exhaustion);

    parse_isovert_command_line(argc, argv, input_info);

    SHARPISO_SCALAR_GRID full_scalar_grid;
    NRRD_INFO nrrd_info;
    read_nrrd_file
      (input_info.scalar_filename, full_scalar_grid,  nrrd_info, io_time);

    GRADIENT_GRID full_gradient_grid;
    NRRD_INFO nrrd_gradient_info;
    std::vector<COORD_TYPE> edgeI_coord;
    std::vector<GRADIENT_COORD_TYPE> edgeI_normal_coord;

    if (input_info.GradientsRequired()) {

      string gradient_filename;

      if (input_info.gradient_filename == NULL) {
        construct_gradient_filename
          (input_info.scalar_filename, gradient_filename);
      }
      else {
        gradient_filename = string(input_info.gradient_filename);
      }

      read_nrrd_file(gradient_filename.c_str(), full_gradient_grid,
                     nrrd_gradient_info);
      flag_gradient = true;

      if (!full_gradient_grid.CompareSize(full_scalar_grid)) {
        error.AddMessage("Input error. Grid mismatch.");
        error.AddMessage
          ("  Dimension or axis sizes of gradient grid and scalar grid do not match.");
        throw error;
      }
    }
    else if (input_info.NormalsRequired()) {

      if (input_info.normal_filename == NULL) {
        error.AddMessage("Programming error.  Missing normal filename.");
        throw error;
      }

      read_off_file
        (input_info.normal_filename, edgeI_coord, edgeI_normal_coord);
    }

    if (!check_input(input_info, full_scalar_grid, error))
      { throw(error); };

    // copy nrrd_info into input_info
    set_input_info(nrrd_info, input_info);

    // set DUAL datastructures and flags
    ISODUAL_DATA isodual_data;
    isodual_data.grad_selection_cube_offset = 0.1;

    if (flag_gradient) {
      isodual_data.SetGrids
        (full_scalar_grid, full_gradient_grid,
         input_info.flag_subsample, input_info.subsample_resolution,
         input_info.flag_supersample, input_info.supersample_resolution);
    }
    else
    {
      isodual_data.SetScalarGrid
        (full_scalar_grid, input_info.flag_subsample, 
         input_info.subsample_resolution,
         input_info.flag_supersample, input_info.supersample_resolution);

      if (input_info.NormalsRequired()) {
        isodual_data.SetEdgeI(edgeI_coord, edgeI_normal_coord);
      }

    }
    // Note: isodual_data.SetScalarGrid or isodual_data.SetGrids
    //       must be called before set_mesh_data.
    set_isodual_data(input_info, isodual_data, isodual_time);
    if (input_info.flag_output_param) 
      { report_isodual_param(isodual_data); }

    if (input_cube_coord.size() == DIM3) {
      input_cube_index = 
        isodual_data.ScalarGrid().ComputeVertexIndex(&(input_cube_coord[0]));
      flag_input_cube = true;
    }
    else {
      flag_input_cube = false;

      report_num_cubes(full_scalar_grid, input_info, isodual_data);
    }

    print_isovert_info(input_info, isodual_data);
  }
  catch (ERROR error) {
    if (error.NumMessages() == 0) {
      cerr << "Unknown error." << endl;
    }
    else { error.Print(cerr); }
    cerr << "Exiting." << endl;
    exit(20);
  }
  catch (...) {
    cerr << "Unknown error." << endl;
    exit(50);
  };

}

void print_isovert_info
(const INPUT_INFO & input_info, const ISODUAL_DATA & isodual_data)
{
  const int dimension = isodual_data.ScalarGrid().Dimension();
  const AXIS_SIZE_TYPE * axis_size = isodual_data.ScalarGrid().AxisSize();
  const int num_cube_vertices = compute_num_cube_vertices(dimension);
  const int num_cubes = isodual_data.ScalarGrid().ComputeNumCubes();
  const int bin_width = isodual_data.bin_width;
  const bool allow_multiple_iso_vertices =
    isodual_data.allow_multiple_iso_vertices;
  const bool flag_check_disk = isodual_data.flag_check_disk;
  const VERTEX_POSITION_METHOD vertex_position_method
    = isodual_data.vertex_position_method;
  const int dist2cube = 1;
  std::vector<VERTEX_INDEX> gcube_map;
  GRID_COORD_TYPE cube_coord[DIM3];
  IJK::BOX<COORD_TYPE> box(DIM3);
  std::vector<VERTEX_INDEX> quad_vert;
  std::vector<VERTEX_INDEX> isoquad_cube;
  std::vector<ISODUAL3D::DUAL_ISOVERT> iso_vlist;
  std::vector<ISODUAL3D::CUBE_ISOVERT_DATA> cube_isovert_data;
  std::vector<ISO_VERTEX_INDEX> tri_vert2;
  std::vector<ISO_VERTEX_INDEX> quad_vert2;
  SHARPISO_EDGE_INDEX_GRID edge_index(DIM3, axis_size, DIM3);

  edge_index.SetAllCoord(ISOVERT::NO_INDEX);
  if (isodual_data.AreEdgeISet()) 
    { set_edge_index(isodual_data.EdgeICoord(), edge_index); }

  box.SetAllMinCoord(0);
  box.SetMaxCoord(isodual_data.ScalarGrid().AxisSize());
  if (input_info.minc.size() == DIM3) 
    { box.SetMinCoord(vector2pointer(input_info.minc)); };
  if (input_info.maxc.size() == DIM3) 
    { box.SetMaxCoord(vector2pointer(input_info.maxc)); };

  bool flag_separate_opposite(true);
  bool flag_separate_neg = isodual_data.flag_separate_neg;
  IJKDUALTABLE::ISODUAL_CUBE_TABLE 
    isodual_table(dimension, flag_separate_neg, flag_separate_opposite);

  for (unsigned int i = 0; i < input_info.isovalue.size(); i++) {

    const SCALAR_TYPE isovalue = input_info.isovalue[i];

    ISOVERT isovert;
    DUAL_ISOSURFACE dual_isosurface;
    ISODUAL_INFO isodual_info;
    ISOVERT_INFO isovert_info;

    BIN_GRID<VERTEX_INDEX> bin_grid;
    init_bin_grid(isodual_data.ScalarGrid(), bin_width, bin_grid);

    if (isodual_data.IsGradientGridSet() &&
        vertex_position_method == GRADIENT_POSITIONING
        || vertex_position_method == EDGEI_INTERPOLATE
        || vertex_position_method == EDGEI_GRADIENT) {

      compute_dual_isovert
        (isodual_data.ScalarGrid(), isodual_data.GradientGrid(),
         isovalue, isodual_data, vertex_position_method, 
         isovert, isovert_info);

      select_sharp_isovert
        (isodual_data.ScalarGrid(), isovalue, isodual_data, isovert);

      if (isodual_data.flag_recompute_isovert) {
        recompute_isovert_positions
          (isodual_data.ScalarGrid(), isodual_data.GradientGrid(),
           isovalue, isodual_data, isovert);
      }
    }
    else if (isodual_data.AreEdgeISet() &&
             isodual_data.VertexPositionMethod() == EDGEI_INPUT_DATA) {
      compute_dual_isovert
        (isodual_data.ScalarGrid(), 
         isodual_data.EdgeICoord(), isodual_data.EdgeINormalCoord(),
         isovalue, isodual_data, isovert);
    }
    else {
      cerr << "Input error. Missing gradient or normal information." << endl;
      exit(30);
    }

    insert_selected_in_bin_grid(isovert, bin_width, bin_grid);

    if (allow_multiple_iso_vertices) {

      std::vector<FACET_VERTEX_INDEX> facet_vertex;
      std::vector<ISO_VERTEX_INDEX> iso_vlist_cube;
      std::vector<FACET_VERTEX_INDEX> iso_vlist_patch;

      extract_dual_isopoly
        (isodual_data.ScalarGrid(), isovalue, isoquad_cube, 
         facet_vertex, isodual_info);

      map_isopoly_vert(isovert, isoquad_cube);

      full_split_dual_isovert
        (isodual_data.ScalarGrid(), isodual_table, isovalue,
         isovert, isoquad_cube, facet_vertex, isodual_data,
         iso_vlist, quad_vert, cube_isovert_data, isodual_info.sharpiso);

      const NUM_TYPE num_gcube = isovert.gcube_list.size();
      gcube_map.resize(num_gcube);

      merge_sharp_iso_vertices_multi
        (isodual_data.ScalarGrid(), isodual_table, isovalue, iso_vlist, isovert, 
         isodual_data, quad_vert, gcube_map, isodual_info.sharpiso);
    }
    else {
      extract_dual_isopoly
        (isodual_data.ScalarGrid(), isovalue, isoquad_cube, isodual_info);

      map_isopoly_vert(isovert, isoquad_cube);

      const NUM_TYPE num_gcube = isovert.gcube_list.size();
      gcube_map.resize(num_gcube);
      merge_sharp_iso_vertices
        (isodual_data.ScalarGrid(), isovalue, isovert, isodual_data,
         isoquad_cube, gcube_map, isodual_info.sharpiso);
    }

    for (int i = 0; i < isovert.gcube_list.size(); i++) {
      VERTEX_INDEX cube_index = isovert.gcube_list[i].cube_index;

      if (flag_input_cube) {

        if (cube_index == input_cube_index) {

          out_gcube(cout, isovalue, isodual_data, isovert, i,
                    edge_index, gcube_map, bin_grid);
          cout << endl;

          if (flag_list_gradients || flag_list_normalized_gradients) {
            out_gradients(cout, isodual_data.ScalarGrid(), 
                          isodual_data.GradientGrid(), cube_index,
                          isovalue, isodual_data);
            cout << endl;
          }

          if (flag_disk_info) {

            out_neighborhood
              (cout, isodual_data.ScalarGrid(), cube_index, isovert, gcube_map);

            std::vector<ISODUAL3D::DUAL_ISOVERT> iso_vlist2;
            if (allow_multiple_iso_vertices) {
              extract_dual_isopatch_incident_on_multi
                (isodual_data.ScalarGrid(), isodual_table, isovalue, 
                 isovert, cube_index, gcube_map, dist2cube, 
                 tri_vert2, quad_vert2, iso_vlist2);

              cout << "Neighboring cubes with multiple vertices: " << endl;
              for (int i = 0; i < iso_vlist2.size(); i++) {
                IJKDUALTABLE::TABLE_INDEX table_index = iso_vlist2[i].table_index;
                NUM_TYPE patch_index = iso_vlist2[i].patch_index;
                NUM_TYPE num_isov = isodual_table.NumIsoVertices(table_index);
                if (num_isov > 1) {
                  cout << "  Isovert: " << i
                       << "  Cube: " << iso_vlist2[i].cube_index
                       << "  Table index: " << table_index
                       << "  Num iso vert: " << num_isov << endl;
                }
              }
              cout << endl;
            }
            else {
              extract_dual_isopatch_incident_on
                (isodual_data.ScalarGrid(), isovalue, isovert, cube_index,
                 gcube_map, dist2cube, tri_vert2, quad_vert2);
            }

            IJK::reorder_quad_vertices(quad_vert2);

            cout << "Isosurface triangles: ";
            for (NUM_TYPE j = 0; j < tri_vert2.size()/NUM_VERT_PER_TRI; j++) {
              cout << " ";
              IJK::print_list(cout, &(tri_vert2[NUM_VERT_PER_TRI*j]), 
                              NUM_VERT_PER_TRI);
            }
            cout << endl;

            cout << "Isosurface quadrilaterals: ";
            for (NUM_TYPE j = 0; j < quad_vert2.size()/NUM_VERT_PER_QUAD; j++) {
              cout << " ";
              IJK::print_list(cout, &(quad_vert2[NUM_VERT_PER_QUAD*j]), 
                              NUM_VERT_PER_QUAD);
            }
            cout << endl << endl;

            if (is_isopatch_disk3D(tri_vert2, quad_vert2)) {
              cout << "      Isopatch is a disk." << endl;
            }
            else {
              cout << "      Isopatch is not a disk." << endl;
            }
          }
        }
      }
      else {
        isodual_data.ScalarGrid().ComputeCoord(cube_index, cube_coord);
        if (box.Contains(cube_coord)) {
          if (allow_multiple_iso_vertices) {
            out_gcube_multi(cout, isovalue, isodual_data, isodual_table, 
                            isovert, i, edge_index, cube_isovert_data,
                            gcube_map, bin_grid);
          }
          else {
            out_gcube(cout, isovalue, isodual_data, isovert, i,
                      edge_index, gcube_map, bin_grid);
          }
        }
      }
    }
  }
}

void insert_selected_in_bin_grid
(const ISOVERT & isovert, const AXIS_SIZE_TYPE bin_width,
 BIN_GRID<VERTEX_INDEX> & bin_grid)
{
  for (NUM_TYPE i = 0; i < isovert.gcube_list.size(); i++) {
    if (isovert.gcube_list[i].flag == SELECTED_GCUBE) {
      VERTEX_INDEX cube_index = isovert.gcube_list[i].cube_index;
      bin_grid_insert
        (isovert.sharp_ind_grid, bin_width, cube_index, bin_grid);
    }
  }
}

void out_gcube_multi
(std::ostream & out, const SCALAR_TYPE isovalue, 
 const ISODUAL_DATA & isodual_data,
 const IJKDUALTABLE::ISODUAL_CUBE_TABLE & isodual_table,
 const ISOVERT & isovert,
 const NUM_TYPE gcube_index,
 const SHARPISO_EDGE_INDEX_GRID & edge_index,
 const std::vector<ISODUAL3D::CUBE_ISOVERT_DATA> & cube_isovert_data,
 const std::vector<VERTEX_INDEX> & gcube_map,
 const BIN_GRID<VERTEX_INDEX> & bin_grid)
{
  ISODUAL3D_CUBE_FACE_INFO cube(DIM3);
  IJK::ARRAY<COORD_TYPE> isov_coord(DIM3);

  VERTEX_INDEX cube_index = isovert.gcube_list[gcube_index].cube_index;

  out_gcube(out, isovalue, isodual_data, isovert.gcube_list[gcube_index], 
            edge_index);
  IJKDUALTABLE::TABLE_INDEX table_index = 
    cube_isovert_data[gcube_index].table_index;
  NUM_TYPE num_isov = isodual_table.NumIsoVertices(table_index);
  out << "      Table index: " << table_index 
      << ".  Num iso vertices: " << num_isov
      << "." << endl;

  if (isovert.gcube_list[gcube_index].flag == SMOOTH_GCUBE ||
      isovert.gcube_list[gcube_index].flag == UNAVAILABLE_GCUBE ||
      isovert.gcube_list[gcube_index].flag == NON_DISK_GCUBE ) {

    if (num_isov > 1) {
      out << "      Smooth vertex locations: ";
      for (NUM_TYPE j = 0; j < num_isov; j++) {

        compute_isosurface_grid_edge_centroid
          (isodual_data.ScalarGrid(), isodual_table, isovalue, cube_index, j,
           table_index, cube, isov_coord.Ptr());
        print_coord3D(out, isov_coord.Ptr());
        out << "  ";
      }
      out << endl;
    }
  }

  if (gcube_map[gcube_index] != gcube_index) {
    out << "      Mapped to isovert for cube: "
        << isovert.gcube_list[gcube_map[gcube_index]].cube_index << endl;
  }

  if (isovert.gcube_list[gcube_index].flag == UNAVAILABLE_GCUBE) {
    const bool flag_check_angle = isodual_data.flag_check_triangle_angle;
    const int bin_width = isodual_data.bin_width;
    VERTEX_INDEX iv1, iv2;

    if (creates_triangle
        (isodual_data.ScalarGrid(), flag_check_angle, isovert,
         cube_index, isovalue, bin_grid, bin_width, iv1, iv2)) {

      out << "      Isosurface vertex creates triangle with vertices from cubes "
          << iv1 << " and " << iv2 << "." << endl;
    }
    else {
      out << "      Cube UNAVAILABLE but does not create triangle!?!" << endl;
    }
  }

}

void out_gcube
(std::ostream & out, const SCALAR_TYPE isovalue, 
 const ISODUAL_DATA & isodual_data,
 const ISOVERT & isovert,
 const NUM_TYPE gcube_index,
 const SHARPISO_EDGE_INDEX_GRID & edge_index,
 const std::vector<VERTEX_INDEX> & gcube_map,
 const BIN_GRID<VERTEX_INDEX> & bin_grid)
{
  const VERTEX_INDEX cube_index = isovert.gcube_list[gcube_index].cube_index;
  const int dist2cube = 1;
  std::vector<ISO_VERTEX_INDEX> tri_vert;
  std::vector<ISO_VERTEX_INDEX> quad_vert;

  out_gcube(out, isovalue, isodual_data, isovert.gcube_list[gcube_index],
            edge_index); 

  if (gcube_map[gcube_index] != gcube_index) {
    out << "      Mapped to isovert for cube: "
        << isovert.gcube_list[gcube_map[gcube_index]].cube_index << endl;
  }

  if (isovert.gcube_list[gcube_index].flag == SELECTED_GCUBE) {

    extract_dual_isopatch_incident_on
      (isodual_data.ScalarGrid(), isovalue, isovert, cube_index,
       gcube_map, dist2cube, tri_vert, quad_vert);

    IJK::reorder_quad_vertices(quad_vert);

    if (!is_isopatch_disk3D(tri_vert, quad_vert)) {
      out << "      Isopatch is not a disk.";
      out << endl;
    }

  }
  else if (isovert.gcube_list[gcube_index].flag == UNAVAILABLE_GCUBE) {
    const bool flag_check_angle = isodual_data.flag_check_triangle_angle;
    const int bin_width = isodual_data.bin_width;
    VERTEX_INDEX iv1, iv2;

    if (creates_triangle
        (isodual_data.ScalarGrid(), flag_check_angle, isovert,
         cube_index, isovalue, bin_grid, bin_width, iv1, iv2)) {

      out << "      Isosurface vertex creates triangle with vertices from cubes "
          << iv1 << " and " << iv2 << "." << endl;
    }
    else {
      out << "      Cube UNAVAILABLE but does not create triangle!?!" << endl;
    }
  }

}

void out_gcube
(std::ostream & out, const SCALAR_TYPE isovalue, 
 const ISODUAL_DATA & isodual_data, const GRID_CUBE & gcube,
 const SHARPISO_EDGE_INDEX_GRID & edge_index)
{
  const VERTEX_INDEX cube_index = gcube.cube_index;
  GRID_COORD_TYPE cube_coord[DIM3];
  COORD_TYPE isovert_coord[DIM3];
  EIGENVALUE_TYPE eigenvalues[DIM3];
  EIGENVALUE_TYPE normalized_eigenvalues[DIM3];
  NUM_TYPE num_large_eigenvalues;

  isodual_data.ScalarGrid().ComputeCoord(cube_index, cube_coord);

  // Initialize eigenvalues[], normalized_eigenvalues[]
  IJK::set_coord_3D(0, eigenvalues);
  IJK::set_coord_3D(0, normalized_eigenvalues);

  compute_eigenvalues
    (isovalue, isodual_data, gcube, edge_index,
     eigenvalues, normalized_eigenvalues, num_large_eigenvalues);

  out << "Cube: " << setw(6) << cube_index << " ";
  print_coord3D(out, cube_coord);
  out << ". Type ";
  out_gcube_type(out, gcube.flag);
  out << ".  Isovert: ";
  IJK::print_coord3D(out, gcube.isovert_coord);
  out << ". Linf dist: " << gcube.linf_dist << ".";
  out << endl;
  out << "  ";
  out << "    # eigen: " << int(gcube.num_eigenvalues);
  out << ".  Eigenvalues: ";
  print_coord3D(out, eigenvalues);
  out << ".  Normalized eigenvalues: ";
  print_coord3D(out, normalized_eigenvalues);
  out << "." << endl;
}

void out_gcube_type(std::ostream & out, const GRID_CUBE_FLAG flag)
{
  out << setw(9);
  switch(flag) {
  case (AVAILABLE_GCUBE):
    out << "Available";
    break;

  case (SELECTED_GCUBE):
    out << "Selected";
    break;

  case (COVERED_GCUBE):
    out << "Covered";
    break;

  case (UNAVAILABLE_GCUBE):
    out << "Unavailable";
    break;

  case (SMOOTH_GCUBE):
    out << "Smooth";
    break;

  case (NON_DISK_GCUBE):
    out << "Non-disk";
    break;

  default:
    out << "Unknown";
    break;
  }
}

void compute_eigenvalues
(const SCALAR_TYPE isovalue, 
 const ISODUAL_DATA & isodual_data, const GRID_CUBE & gcube,
 const SHARPISO_EDGE_INDEX_GRID & edge_index,
 EIGENVALUE_TYPE eigenvalues[DIM3], 
 EIGENVALUE_TYPE normalized_eigenvalues[DIM3],
 NUM_TYPE num_large_eigenvalues)
{
  const VERTEX_INDEX cube_index = gcube.cube_index;
	const SIGNED_COORD_TYPE grad_selection_cube_offset =
			isodual_data.grad_selection_cube_offset;
  COORD_TYPE isovert_coord[DIM3];
	OFFSET_CUBE_111 cube_111(grad_selection_cube_offset);
	SVD_INFO svd_info;

  if (isodual_data.IsGradientGridSet()) {

    if (isodual_data.vertex_position_method == GRADIENT_POSITIONING) {

      if (isodual_data.use_lindstrom) {
        svd_compute_sharp_vertex_for_cube_lindstrom
          (isodual_data.ScalarGrid(), isodual_data.GradientGrid(), cube_index, 
           isovalue, isodual_data, cube_111, isovert_coord,
           eigenvalues, num_large_eigenvalues, svd_info);
      }
      else {
        svd_compute_sharp_vertex_for_cube_lc_intersection
          (isodual_data.ScalarGrid(), isodual_data.GradientGrid(), cube_index, 
           isovalue, isodual_data, cube_111, isovert_coord,
           eigenvalues, num_large_eigenvalues, svd_info);
      }
    }
    else if (isodual_data.vertex_position_method == EDGEI_GRADIENT) {
      svd_compute_sharp_vertex_edgeI_sharp_gradient
        (isodual_data.ScalarGrid(), isodual_data.GradientGrid(), cube_index, 
         isovalue, isodual_data, isovert_coord,
         eigenvalues, num_large_eigenvalues, svd_info);
    }
    else {
      svd_compute_sharp_vertex_edgeI_interpolate_gradients
        (isodual_data.ScalarGrid(), isodual_data.GradientGrid(), cube_index, 
         isovalue, isodual_data, isovert_coord,
         eigenvalues, num_large_eigenvalues, svd_info);
    }

  }
  else if (isodual_data.AreEdgeISet()) {
    svd_compute_sharp_vertex_for_cube_hermite
      (isodual_data.ScalarGrid(), 
       isodual_data.EdgeICoord(), isodual_data.EdgeINormalCoord(),
       edge_index, cube_index, 
       isovalue, isodual_data, isovert_coord,
       eigenvalues, num_large_eigenvalues, svd_info);
  }
  else {
    cerr << "Input error. Missing gradient or normal information." << endl;
    exit(30);
  }

  if (num_large_eigenvalues != gcube.num_eigenvalues) {
    cerr << "Warning: Number of large eigenvalues mismatch for cube "
         << cube_index << endl;
    cerr << "  gcube.num_eigen: " << int(gcube.num_eigenvalues) << ".";
    cerr << "  num_large_eigen: "
         << num_large_eigenvalues << endl;
  }

  if (!isodual_data.flag_recompute_isovert ||
      gcube.flag != UNAVAILABLE_GCUBE) {

    if (!is_coord_equal(DIM3, isovert_coord, gcube.isovert_coord)) {
      cerr << "Warning: Isovertex coordinate mismatch for cube "
           << cube_index << "." << endl;
      cerr << "  gcube.isovert_coord[] = ";
      print_coord3D(cerr, gcube.isovert_coord);
      cerr << ".  isovert_coord[] = ";
      print_coord3D(cerr, isovert_coord);
      cerr << "." << endl;
    }
  }

  if (eigenvalues[0] > isodual_data.max_small_eigenvalue) {
    multiply_coord_3D(1.0/eigenvalues[0], eigenvalues, normalized_eigenvalues);
  }
  else {
    set_coord_3D(0, normalized_eigenvalues);
  }
}

void out_gradients
(std::ostream & out, const SHARPISO_SCALAR_GRID_BASE & scalar_grid,
 const GRADIENT_GRID_BASE & gradient_grid, const VERTEX_INDEX cube_index,
 const SCALAR_TYPE isovalue, const ISODUAL_PARAM & param)
{
	const SIGNED_COORD_TYPE grad_selection_cube_offset =
    param.grad_selection_cube_offset;
	OFFSET_CUBE_111 cube_111(grad_selection_cube_offset);
	std::vector<COORD_TYPE> point_coord;
	std::vector<GRADIENT_COORD_TYPE> gradient_coord;
	std::vector<SCALAR_TYPE> scalar;
  GRADIENT_COORD_TYPE normalized_gradient_coord[DIM3];
  COORD_TYPE isop_coord[DIM3];
  COORD_TYPE w[DIM3];
  COORD_TYPE cube_center[DIM3];
  int num_gradients;


  scalar_grid.ComputeCubeCenterCoord(cube_index, cube_center);

	get_gradients
    (scalar_grid, gradient_grid, cube_index, isovalue,
     param, cube_111, false,
     point_coord, gradient_coord, scalar, num_gradients);

  for (NUM_TYPE i = 0; i < num_gradients; i++) {
    GRADIENT_COORD_TYPE magnitude;
    IJK::compute_magnitude(DIM3, &(gradient_coord[i*DIM3]), magnitude);

    COORD_TYPE distance;
    compute_signed_distance_to_gfield_plane
      (&(gradient_coord[i*DIM3]), &(point_coord[i*DIM3]), scalar[i], 
       cube_center, isovalue, distance);

    out << "Point " << setw(2)  << i << " ";
    IJK::print_coord3D(out, &(point_coord[i*DIM3]));
    out << " :  Scalar " << scalar[i];
    if (flag_list_gradients) {
      out << " Grad ";
      IJK::print_coord3D(out, &(gradient_coord[i*DIM3]));
    }

    if (flag_list_normalized_gradients) {
      out << " Normalized grad ";
      IJK::normalize_vector
        (DIM3, &(gradient_coord[i*DIM3]), 0.0, normalized_gradient_coord);
      IJK::print_coord3D(out, normalized_gradient_coord);
    }
    if (flag_list_iso_points) {
      if (magnitude > 0) {
        GRADIENT_COORD_TYPE scale = 
          (isovalue - scalar[i])/(magnitude*magnitude);
        IJK::multiply_coord_3D(scale, &(gradient_coord[i*DIM3]), w);
        IJK::add_coord_3D(&(point_coord[i*DIM3]), w, isop_coord);
        out << " Iso point ";
        IJK::print_coord3D(out, isop_coord);
      }
    }
    out << "  Mag " << magnitude;
    out << "  Dist " << distance;
    out << endl;
  }

}


void out_neighborhood
(std::ostream & out, const SHARPISO_SCALAR_GRID_BASE & scalar_grid,
 const VERTEX_INDEX cube_index,
 const ISOVERT & isovert, const std::vector<VERTEX_INDEX> & gcube_map)
{
  const AXIS_SIZE_TYPE axis_size_4x4x4[DIM3] = { 4, 4, 4 };
  const AXIS_SIZE_TYPE axis_size_3x3x3[DIM3] = { 3, 3, 3 };
  SHARPISO_SCALAR_GRID scalar_subgrid(DIM3, axis_size_4x4x4);
  SHARPISO_INDEX_GRID gcube_map_grid(DIM3, axis_size_3x3x3);

  VERTEX_INDEX iv0 = 
    cube_index - scalar_grid.CubeVertexIncrement(NUM_CUBE_VERTICES3D-1);

  scalar_subgrid.CopyRegion(scalar_grid, iv0, axis_size_4x4x4, 0);

  out << "Scalar values around cube " << cube_index << ":" << endl;
  IJK::output_scalar_grid(cout, scalar_subgrid);
  out << endl;

  gcube_map_grid.CopyRegion(isovert.sharp_ind_grid, iv0, axis_size_3x3x3, 0);

  for (VERTEX_INDEX iv = 0; iv < gcube_map_grid.NumVertices(); iv++) {
    INDEX_DIFF_TYPE gcube_index = gcube_map_grid.Scalar(iv);
    if (0 <= gcube_index && gcube_index < gcube_map.size())
      { gcube_map_grid.Set(iv, gcube_map[gcube_index]); }
  }

  out << "gcube map around cube " << cube_index << ": " << endl;
  output_scalar_grid(cout, gcube_map_grid);
  out << endl;
}


// **************************************************
// PARSE COMMAND LINE
// **************************************************

int get_option_int
(const char * option, const char * value_string)
{
  int x;
  if (!IJK::string2val(value_string, x)) {
    cerr << "Error in argument for option: " << option << endl;
    cerr << "Non-integer character in string: " << value_string << endl;
    exit(50);
  }
  
  return(x);
}

float get_option_float
(const char * option, const char * value_string)
{
  float x;
  if (!IJK::string2val(value_string, x)) {
    cerr << "Error in argument for option: " << option << endl;
    cerr << "Non-numeric character in string: " << value_string << endl;
    exit(50);
  }

  return(x);
}

/// Get string and convert to list of arguments.
template <typename ETYPE>
void get_option_multiple_arguments
(const char * option, const char * value_string, std::vector<ETYPE> & v)
{
  if (!IJK::string2vector(value_string, v)) {
    cerr << "Error in argument for option: " << option << endl;
    cerr << "Non-numeric character in string: " << value_string << endl;
    exit(50);
  }
}

// Parse the command line.
void parse_isovert_command_line
(int argc, char **argv, INPUT_INFO & input_info)
{
	if (argc == 1) { usage_error(argv[0]); };

	int iarg = 1;
	while (iarg < argc) {

    int next_arg;
    if (!parse_command_option(argc, argv, iarg, next_arg, input_info) ||
        iarg == next_arg) {

      if (argv[iarg][0] == '-') {
        string s = argv[iarg];
        if (s == "-help") { 
          help(argv[0]);
          next_arg = iarg+1;
        }
        else if (s == "-cc") {
          if (iarg+1 < argc) {
            get_option_multiple_arguments
              (argv[iarg], argv[iarg+1], input_cube_coord);
            next_arg = iarg+2;
          }
          else
            { usage_error(argv[0]); }
        }
        else if (s == "-listg") {
          flag_list_gradients = true;
          next_arg = iarg+1;
        }
        else if (s == "-listng") {
          flag_list_normalized_gradients = true;
          next_arg = iarg+1;
        }
        else if (s == "-list_isop") {
          flag_list_iso_points = true;
          next_arg = iarg+1;
        }
        else if (s == "-disk_info") {
          flag_disk_info = true;
          next_arg = iarg+1;
        }
        else
          { break; }
      }
      else
        { break; }
    }

    iarg = next_arg;
  }

  if (flag_list_iso_points) {
    if (!flag_list_gradients && !flag_list_normalized_gradients)
      { flag_list_gradients = true; }
  }

  if (!flag_disk_info && !flag_list_gradients && 
      !flag_list_normalized_gradients)
    { flag_disk_info = true; }

  parse_isovalue_and_filename(argc, argv, iarg, input_info);
  set_input_info_defaults(input_info);
  check_input_info(input_info);
}

void memory_exhaustion()
{
  cerr << "Error: Out of memory.  Terminating program." << endl;
  exit(10);
}

