/// \file ijkmesh.txx
/// ijk templates for handling polyhedral meshes
/// Version 0.1.0

/*
  IJK: Isosurface Jeneration Kode
  Copyright (C) 2010,2012 Rephael Wenger

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

#ifndef _IJKMESH_
#define _IJKMESH_

#include "ijk.txx"
#include <vector>

namespace IJK {

  // **************************************************
  // FUNCTION CLASS FOR TUPLE COMPARISON
  // **************************************************

  /// comparison function template
  template <class DTYPE, class T> class TUPLE_LESS_THAN {
  protected:
    const DTYPE dimension;
    const T * array;

  public:
    TUPLE_LESS_THAN(const DTYPE d, const T * a):
      dimension(d), array(a) {};
  

    bool operator ()(const int i0, const int i1) const
    { 
      const T * p0 = i0*dimension+array;
      const T * p1 = i1*dimension+array;

      return(std::lexicographical_compare(p0, p0+dimension, p1, p1+dimension)); 
    };
  };

  // **************************************************
  // COMPUTE SIMPLEX FACETS
  // **************************************************

  /// Compute simplex facets
  template <typename DTYPE, typename SVERT, typename NTYPE,
            typename FVERT, typename FACET_INDEX>
  void compute_simplex_facets
  (const DTYPE simplex_dimension, 
   const SVERT * simplex_vertex, const NTYPE num_simplices,
   std::vector<FVERT> & facet_vertex, 
   std::vector<FACET_INDEX> & simplex_facet)
  {
    const NTYPE numv_per_simplex = simplex_dimension+1;
    const NTYPE num_facets_per_simplex = simplex_dimension+1;
    const NTYPE numv_per_facet =  numv_per_simplex-1;
    const NTYPE total_num_facets = num_simplices*num_facets_per_simplex;
    IJK::PROCEDURE_ERROR error("compute_simplex_facets");

    facet_vertex.clear();
    simplex_facet.clear();

    if (num_simplices == 0) { return; };
    if (num_facets_per_simplex < 1) { return; }

    IJK::ARRAY<FVERT> facet_vertex2(total_num_facets*numv_per_facet);

    // store facets in facet_vertex2[]
    NTYPE m = 0;
    for (NTYPE i = 0; i < num_simplices; i++) {
      const SVERT * firstv = simplex_vertex + i*numv_per_simplex;
      for (NTYPE j = 0; j < num_facets_per_simplex; j++) {
        for (NTYPE k = 0; k < numv_per_simplex; k++) {
          if (j != k) {
            facet_vertex2[m] = firstv[k];
            m++;
          }
        }
      }
    }

    if (m != total_num_facets*numv_per_facet) {
      error.AddMessage
        ("Programming error.  Incorrect number of vertices in facet_vertex2[].");
      throw error;
    }

    // Sort facet vertices
    for (NTYPE i = 0; i < total_num_facets; i++) {
      SVERT * firstv = facet_vertex2.Ptr() + i*numv_per_facet;
      std::sort(firstv, firstv+numv_per_facet);
    }

    // Sort facets
    IJK::ARRAY<NTYPE> index_sorted(total_num_facets);
    for (NTYPE i = 0; i < total_num_facets; i++) 
      { index_sorted[i] = i; }

    TUPLE_LESS_THAN<DTYPE, NTYPE> facet_less_than
      (numv_per_facet, facet_vertex2.PtrConst());
    sort(index_sorted.Ptr(), index_sorted.Ptr()+total_num_facets, 
         facet_less_than);

    // Count number of non-duplicate facets
    simplex_facet.resize(total_num_facets);
    NTYPE num_non_duplicates = 1;
    simplex_facet[0] = 0;
    for (NTYPE i = 1; i < total_num_facets; i++) {
      NTYPE j = index_sorted[i-1];
      NTYPE k = index_sorted[i];
      const FVERT * firstv_j = facet_vertex2.PtrConst()+j*numv_per_facet;
      const FVERT * firstv_k = facet_vertex2.PtrConst()+k*numv_per_facet;

      if (!equal(firstv_j, firstv_j+numv_per_facet, firstv_k))
        { num_non_duplicates++; };

      simplex_facet[k] = num_non_duplicates-1;
    }

    facet_vertex.resize(num_non_duplicates*numv_per_facet);

    // Copy first facet from facet_vertex2[] into facet_vertex[]
    FVERT * fvert = &(facet_vertex[0]);
    FVERT * fvert2 = facet_vertex2.Ptr()+index_sorted[0]*numv_per_facet;
    std::copy(fvert2, fvert2+numv_per_facet, fvert);
    fvert += numv_per_facet;

    // Copy facet_vertex2[] into facet_vertex[], avoiding duplicates
    for (NTYPE i = 1; i < total_num_facets; i++) {
      NTYPE j = index_sorted[i-1];
      NTYPE k = index_sorted[i];
      if (simplex_facet[k] != simplex_facet[j]) {
        // Copy simplex_facet[k] into facet_vertex[]
        SVERT * fvert2 = facet_vertex2.Ptr() + k*numv_per_facet;
        std::copy(fvert2, fvert2+numv_per_facet, fvert);

        fvert += numv_per_facet;
      }
    }

    if (fvert != &(facet_vertex[0]) + num_non_duplicates*numv_per_facet) {
      error.AddMessage("Programming error.  Incorrect number of vertices in facet_vertex[].");
      throw error;
    }
  }

  // **************************************************
  // CONVERT QUADRILATERALS TO TRIANGLES
  // **************************************************

  /// Convert quadrilaterals to triangles.
  /// Quadrilateral vertices are listed in order:
  ///   Lower-left, Lower-right, Upper-Left, Upper-Right
  /// Add new triangles to vector tri_vert.
  template <typename VTYPE0, typename VTYPE1>
  void convert_quad_to_tri
  (const std::vector<VTYPE0> & quad_vert, std::vector<VTYPE1> & tri_vert)
  {
    typedef typename std::vector<VTYPE0>::size_type SIZE_TYPE;

    const SIZE_TYPE NUM_VERT_PER_QUAD = 4;

    for (SIZE_TYPE i = 0; i < quad_vert.size(); i+=NUM_VERT_PER_QUAD) {

      // Get quadrilateral vertices.
      VTYPE0 v0 = quad_vert[i];
      VTYPE0 v1 = quad_vert[i+1];
      VTYPE0 v2 = quad_vert[i+2];
      VTYPE0 v3 = quad_vert[i+3];

      // Add first triangle to tri_vert.
      tri_vert.push_back(v0);
      tri_vert.push_back(v1);
      tri_vert.push_back(v2);

      // Add second triangle to tri_vert.
      tri_vert.push_back(v2);
      tri_vert.push_back(v1);
      tri_vert.push_back(v3);
    }

  }

}

#endif