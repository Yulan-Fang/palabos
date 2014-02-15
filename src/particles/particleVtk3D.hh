/* This file is part of the Palabos library.
 *
 * Copyright (C) 2011-2013 FlowKit Sarl
 * Route d'Oron 2
 * 1010 Lausanne, Switzerland
 * E-mail contact: contact@flowkit.com
 *
 * The most recent release of Palabos can be downloaded at 
 * <http://www.palabos.org/>
 *
 * The library Palabos is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * The library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef PARTICLE_VTK_3D_HH
#define PARTICLE_VTK_3D_HH

#include "core/globalDefs.h"
#include "particles/particleVtk3D.h"
#include "particles/particleNonLocalTransfer3D.h"

#include <cstdio>
#include <cstdlib>

#define frand() ((double) rand() / (RAND_MAX + 1.0))

namespace plb {

template<typename T, template<typename U> class Descriptor>
void writeSurfaceVTK( TriangleBoundary3D<T> const& boundary,
                      MultiParticleField3D<DenseParticleField3D<T,Descriptor> >& particles,
                      std::vector<std::string> const& scalars,
                      std::vector<std::string> const& vectors,
                      std::string const& fName, bool dynamicMesh, plint tag )
{
    writeSurfaceVTK( boundary, particles, scalars, vectors, fName, dynamicMesh, tag,
                     std::vector<T>(), std::vector<T>() );
}

template<typename T, template<typename U> class Descriptor>
void writeSurfaceVTK( TriangleBoundary3D<T> const& boundary,
                      MultiParticleField3D<DenseParticleField3D<T,Descriptor> >& particles,
                      std::vector<std::string> const& scalars,
                      std::vector<std::string> const& vectors,
                      std::string const& fName, bool dynamicMesh, plint tag,
                      std::vector<T> const& scalarFactor, std::vector<T> const& vectorFactor )
{
    SparseBlockStructure3D blockStructure(particles.getBoundingBox());
    blockStructure.addBlock(particles.getBoundingBox(), 0);
    plint envelopeWidth=1;
    MultiBlockManagement3D serialMultiBlockManagement (
            blockStructure, new OneToOneThreadAttribution, envelopeWidth );

    MultiParticleField3D<DenseParticleField3D<T,Descriptor> > multiSerialParticles (
            serialMultiBlockManagement,
            defaultMultiBlockPolicy3D().getCombinedStatistics() );

    copy ( particles, particles.getBoundingBox(), multiSerialParticles, particles.getBoundingBox() );
    if (global::mpi().isMainProcessor()) {
        ParticleField3D<T,Descriptor>& atomicSerialParticles =
            dynamic_cast<ParticleField3D<T,Descriptor>&>(multiSerialParticles.getComponent(0));

        std::vector<Particle3D<T,Descriptor>*> found;
        SmartBulk3D oneBlockBulk(serialMultiBlockManagement, 0);
        atomicSerialParticles.findParticles(oneBlockBulk.toLocal(particles.getBoundingBox()), found);

        vtkForVertices(found, boundary, scalars, vectors, fName, dynamicMesh, tag, scalarFactor, vectorFactor);
    }
}

template<typename T, template<typename U> class Descriptor>
void vtkForVertices(std::vector<Particle3D<T,Descriptor>*> const& particles,
                    TriangleBoundary3D<T> const& boundary,
                    std::vector<std::string> const& scalars,
                    std::vector<std::string> const& vectors,
                    std::string fName, bool dynamicMesh, plint tag,
                    std::vector<T> const& scalarFactor, std::vector<T> const& vectorFactor )
{
    PLB_ASSERT( scalarFactor.empty() || scalarFactor.size()==scalars.size() );
    PLB_ASSERT( vectorFactor.empty() || vectorFactor.size()==vectors.size() );
    if (dynamicMesh) {
        boundary.pushSelect(0,1); // 0=Open, 1=Dynamic.
    }
    else {
        boundary.pushSelect(0,0); // Open, Static.
    }
    TriangularSurfaceMesh<T> const& mesh = boundary.getMesh();
    // If this assertion fails, a likely explanation is that the margin of your sparse block
    // structure is too small, and one of the particles was outside the allocated domain.
    // PLB_PRECONDITION((plint)particles.size() == mesh.getNumVertices());
    if((plint)particles.size() != mesh.getNumVertices()) {
        pcout << "Warning: in vtkForVertices, the numer of particles doesn't match the number" << std::endl;
        pcout << "of vertices. There might be black spots in the produced VTK file." << std::endl;
    }
    
    std::ofstream ofile(fName.c_str());
    ofile << "# vtk DataFile Version 3.0\n";
    ofile << "Surface mesh created with Palabos\n";
    ofile << "ASCII\n";
    ofile << "DATASET UNSTRUCTURED_GRID\n";

    ofile << "POINTS " << particles.size()
          << (sizeof(T)==sizeof(double) ? " double" : " float")
          << "\n";

    std::vector<std::vector<T> > scalarData(scalars.size());
    for (pluint iScalar=0; iScalar<scalars.size(); ++iScalar) {
        scalarData[iScalar].resize(mesh.getNumVertices());
    }
    std::vector<std::vector<Array<T,3> > > vectorData(vectors.size());
    for (pluint iVector=0; iVector<vectors.size(); ++iVector) {
        vectorData[iVector].resize(mesh.getNumVertices());
    }
    std::vector<Array<T,3> > posVect(mesh.getNumVertices());
    // Default initialize all data, just in case every vertex hasn't
    // an associated particle.
    for (plint i=0; i<mesh.getNumVertices(); ++i) {
        for (pluint iScalar=0; iScalar<scalars.size(); ++iScalar) {
            scalarData[iScalar][i] = T();
        }
        for (pluint iVector=0; iVector<vectors.size(); ++iVector) {
            vectorData[iVector][i].resetToZero();
        }
        posVect[i].resetToZero();
    }
    for (pluint iParticle=0; iParticle<particles.size(); ++iParticle) {
        plint iVertex = particles[iParticle]->getTag();
        posVect[iVertex] = particles[iParticle]->getPosition();
        posVect[iVertex] *= boundary.getDx();
        posVect[iVertex] += boundary.getPhysicalLocation();
        for (pluint iScalar=0; iScalar<scalars.size(); ++iScalar) {
            T scalar;
            particles[iParticle]->getScalar(iScalar, scalar);
            if (!scalarFactor.empty()) {
                scalar *= scalarFactor[iScalar];
            }
            scalarData[iScalar][iVertex] = scalar;
        }
        for (pluint iVector=0; iVector<vectors.size(); ++iVector) {
            Array<T,3> vector;
            particles[iParticle]->getVector(iVector, vector);
            if (!vectorFactor.empty()) {
                vector *= vectorFactor[iVector];
            }
            vectorData[iVector][iVertex] = vector;
        }
    }

    for (plint iVertex=0; iVertex<mesh.getNumVertices(); ++iVertex) {
        ofile << posVect[iVertex][0] << " "
              << posVect[iVertex][1] << " "
              << posVect[iVertex][2] << "\n";
    }
    ofile << "\n";

    plint numWallTriangles=0;
    for (plint iTriangle=0; iTriangle<mesh.getNumTriangles(); ++iTriangle) {
        if ( tag<0 || boundary.getTag(iTriangle)==tag )
        {
            ++numWallTriangles;
        }
    }

    ofile << "CELLS " << numWallTriangles
          << " " << 4*numWallTriangles << "\n";

    for (plint iTriangle=0; iTriangle<mesh.getNumTriangles(); ++iTriangle) {
        plint i0 = mesh.getVertexId(iTriangle, 0);
        plint i1 = mesh.getVertexId(iTriangle, 1);
        plint i2 = mesh.getVertexId(iTriangle, 2);
        if ( tag<0 || boundary.getTag(iTriangle)==tag )
        {
            ofile << "3 " << i0 << " " << i1 << " " << i2 << "\n";
        }
    }
    ofile << "\n";

    ofile << "CELL_TYPES " << numWallTriangles << "\n";
    for (plint i=0; i<numWallTriangles; ++i) {
        ofile << "5\n";
    }
    ofile << "\n";

    ofile << "POINT_DATA " << mesh.getNumVertices() << "\n";
    for (pluint iVector=0; iVector<vectors.size(); ++iVector) {
        ofile << "VECTORS " << vectors[iVector]
              << (sizeof(T)==sizeof(double) ? " double" : " float")
              << "\n";
        for (plint iVertex=0; iVertex<mesh.getNumVertices(); ++iVertex) {
            ofile << vectorData[iVector][iVertex][0] << " "
                  << vectorData[iVector][iVertex][1] << " "
                  << vectorData[iVector][iVertex][2] << "\n";
        }
        ofile << "\n";
    }

    for (pluint iScalar=0; iScalar<scalars.size(); ++iScalar) {
        ofile << "SCALARS " << scalars[iScalar]
              << (sizeof(T)==sizeof(double) ? " double" : " float")
              << " 1\n"
              << "LOOKUP_TABLE default\n";
        for (plint iVertex=0; iVertex<mesh.getNumVertices(); ++iVertex) {
            ofile << scalarData[iScalar][iVertex] << "\n";
        }
        ofile << "\n";
    }
    // Restore mesh selection which was active before calling
    //   this function.
    boundary.popSelect();
}

template<typename T, template<typename U> class Descriptor>
void writeVertexAsciiData (
                      TriangleBoundary3D<T> const& boundary,
                      MultiParticleField3D<DenseParticleField3D<T,Descriptor> >& particles,
                      std::vector<std::string> const& scalars,
                      std::vector<std::string> const& vectors,
                      std::string const& fName, bool dynamicMesh, bool printHeader,
                      std::vector<T> const& scalarFactor, std::vector<T> const& vectorFactor )
{
    SparseBlockStructure3D blockStructure(particles.getBoundingBox());
    blockStructure.addBlock(particles.getBoundingBox(), 0);
    plint envelopeWidth=1;
    MultiBlockManagement3D serialMultiBlockManagement (
            blockStructure, new OneToOneThreadAttribution, envelopeWidth );

    MultiParticleField3D<DenseParticleField3D<T,Descriptor> > multiSerialParticles (
            serialMultiBlockManagement,
            defaultMultiBlockPolicy3D().getCombinedStatistics() );

    copy ( particles, particles.getBoundingBox(), multiSerialParticles, particles.getBoundingBox() );
    if (global::mpi().isMainProcessor()) {
        ParticleField3D<T,Descriptor>& atomicSerialParticles =
            dynamic_cast<ParticleField3D<T,Descriptor>&>(multiSerialParticles.getComponent(0));

        std::vector<Particle3D<T,Descriptor>*> found;
        SmartBulk3D oneBlockBulk(serialMultiBlockManagement, 0);
        atomicSerialParticles.findParticles(oneBlockBulk.toLocal(particles.getBoundingBox()), found);

        vertexAsciiData(found, boundary, scalars, vectors, fName, dynamicMesh, printHeader, scalarFactor, vectorFactor);
    }
}

template<typename T, template<typename U> class Descriptor>
void vertexAsciiData(std::vector<Particle3D<T,Descriptor>*> const& particles,
                         TriangleBoundary3D<T> const& boundary,
                         std::vector<std::string> const& scalars,
                         std::vector<std::string> const& vectors,
                         std::string fName, bool dynamicMesh, bool printHeader,
                         std::vector<T> const& scalarFactor, std::vector<T> const& vectorFactor )
{
    PLB_ASSERT( scalarFactor.empty() || scalarFactor.size()==scalars.size() );
    PLB_ASSERT( vectorFactor.empty() || vectorFactor.size()==vectors.size() );
    if (dynamicMesh) {
        boundary.pushSelect(0,1); // 0=Open, 1=Dynamic.
    }
    else {
        boundary.pushSelect(0,0); // Open, Static.
    }
    TriangularSurfaceMesh<T> const& mesh = boundary.getMesh();
    PLB_PRECONDITION((plint)particles.size() == mesh.getNumVertices());
    std::ofstream ofile(fName.c_str());
    if (printHeader) {
        ofile << "Pos[0] Pos[1] Pos[2]";
        for (pluint iScalar=0; iScalar<scalars.size(); ++iScalar) {
            ofile << " " << scalars[iScalar];
        }
        for (pluint iVector=0; iVector<vectors.size(); ++iVector) {
            ofile << " " << vectors[iVector] << "[0]";
            ofile << " " << vectors[iVector] << "[1]";
            ofile << " " << vectors[iVector] << "[2]";
        }
        ofile << "\n";
    }
    for (pluint iParticle=0; iParticle<particles.size(); ++iParticle) {
        Array<T,3> position = particles[iParticle]->getPosition();
        ofile << position[0] << " " << position[1] << " " << position[2];
        for (pluint iScalar=0; iScalar<scalars.size(); ++iScalar) {
            T scalar;
            particles[iParticle]->getScalar(iScalar, scalar);
            if (!scalarFactor.empty()) {
                scalar *= scalarFactor[iScalar];
            }
            ofile << " " << scalar;
        }
        for (pluint iVector=0; iVector<vectors.size(); ++iVector) {
            Array<T,3> vector;
            particles[iParticle]->getVector(iVector, vector);
            if (!vectorFactor.empty()) {
                vector *= vectorFactor[iVector];
            }
            ofile << " " << vector[0] << " " << vector[1] << " " << vector[2];
        }
        ofile << "\n";
    }
    boundary.popSelect();
}

template<typename T, template<typename U> class Descriptor>
void writeAsciiParticlePos (
        MultiParticleField3D<DenseParticleField3D<T,Descriptor> >& particles,
        std::string const& fName )
{
    SparseBlockStructure3D blockStructure(particles.getBoundingBox());
    blockStructure.addBlock(particles.getBoundingBox(), 0);
    plint envelopeWidth=1;
    MultiBlockManagement3D serialMultiBlockManagement (
            blockStructure, new OneToOneThreadAttribution, envelopeWidth );

    MultiParticleField3D<DenseParticleField3D<T,Descriptor> > multiSerialParticles (
            serialMultiBlockManagement,
            defaultMultiBlockPolicy3D().getCombinedStatistics() );

    copy ( particles, particles.getBoundingBox(), multiSerialParticles, particles.getBoundingBox() );
    if (global::mpi().isMainProcessor()) {
        std::ofstream ofile(fName.c_str());
        ParticleField3D<T,Descriptor>& atomicSerialParticles =
            dynamic_cast<ParticleField3D<T,Descriptor>&>(multiSerialParticles.getComponent(0));

        std::vector<Particle3D<T,Descriptor>*> found;
        SmartBulk3D oneBlockBulk(serialMultiBlockManagement, 0);
        atomicSerialParticles.findParticles(oneBlockBulk.toLocal(particles.getBoundingBox()), found);
        for (pluint iParticle=0; iParticle<found.size(); ++iParticle) {
            Array<T,3> pos(found[iParticle]->getPosition());
            ofile << pos[0] << "," << pos[1] << "," << pos[2] << "\n";
        }
    }
}

template<typename T, template<typename U> class Descriptor>
void particleVtkSerialImplementation (
        MultiParticleField3D<DenseParticleField3D<T,Descriptor> >& multiSerialParticles,
        std::string const& fName,
        std::map<plint,std::string> const& additionalScalars,
        std::map<plint,std::string> const& additionalVectors,
        pluint maxNumParticlesToWrite)
{
    if (global::mpi().isMainProcessor()) {
        ParticleField3D<T,Descriptor>& atomicSerialParticles =
            dynamic_cast<ParticleField3D<T,Descriptor>&>(multiSerialParticles.getComponent(0));

        std::vector<Particle3D<T,Descriptor>*> found;
        SmartBulk3D oneBlockBulk(multiSerialParticles.getMultiBlockManagement(), 0);
        atomicSerialParticles.findParticles(oneBlockBulk.toLocal(multiSerialParticles.getBoundingBox()), found);
        pluint numParticles = found.size();
        pluint numParticlesToWrite = maxNumParticlesToWrite == 0 ? numParticles :
            (maxNumParticlesToWrite > numParticles ? numParticles : maxNumParticlesToWrite);

        std::vector<pluint> iParticles;
        iParticles.resize(0);
        if (numParticlesToWrite != numParticles) {
            for (pluint i = 0; i < numParticlesToWrite; i++)
                iParticles.push_back((pluint) (frand() * (double) (numParticles - 1)));
            std::sort(iParticles.begin(), iParticles.end());
        }

        FILE *fp = fopen(fName.c_str(), "w");
        PLB_ASSERT(fp != NULL);
        fprintf(fp, "# vtk DataFile Version 3.0\n");
        fprintf(fp, "Particle file created by Palabos\n");
        fprintf(fp, "ASCII\n");
        fprintf(fp, "DATASET UNSTRUCTURED_GRID\n");
        fprintf(fp, "POINTS %12lu float\n", (unsigned long) numParticlesToWrite);
        if (numParticlesToWrite == numParticles) {
            for (pluint iParticle = 0; iParticle < numParticles; iParticle++) {
                Array<T,3> pos(found[iParticle]->getPosition());
                fprintf(fp, "% .7e % .7e % .7e\n", (float) pos[0], (float) pos[1], (float) pos[2]);
            }
        } else {
            for (pluint i = 0; i < numParticlesToWrite; i++) {
                Array<T,3> pos(found[iParticles[i]]->getPosition());
                fprintf(fp, "% .7e % .7e % .7e\n", (float) pos[0], (float) pos[1], (float) pos[2]);
            }
        }
        fprintf(fp, "POINT_DATA %12lu\n", (unsigned long) numParticlesToWrite);
        std::map<plint,std::string>::const_iterator vectorIt = additionalVectors.begin();
        for (; vectorIt != additionalVectors.end(); ++vectorIt) {
            plint vectorID = vectorIt->first;
            std::string vectorName = vectorIt->second;
            fprintf(fp, "VECTORS ");
            fprintf(fp, "%s", vectorName.c_str());
            fprintf(fp, " float\n");
            if (numParticlesToWrite == numParticles) {
                for (pluint iParticle = 0; iParticle < numParticles; iParticle++) {
                    Array<T,3> vectorValue;
                    found[iParticle]->getVector(vectorID, vectorValue);
                    Array<float,3> floatVectorValue(vectorValue);
                    fprintf( fp, "% .7e % .7e % .7e\n",
                             (float) floatVectorValue[0], (float) floatVectorValue[1], (float) floatVectorValue[2] );
                }
            } else {
                for (pluint i = 0; i < numParticlesToWrite; i++) {
                    Array<T,3> vectorValue;
                    found[iParticles[i]]->getVector(vectorID, vectorValue);
                    Array<float,3> floatVectorValue(vectorValue);
                    fprintf( fp, "% .7e % .7e % .7e\n",
                             (float) floatVectorValue[0], (float) floatVectorValue[1], (float) floatVectorValue[2] );
                }
            }
        }
        fprintf(fp, "SCALARS Tag float\n");
        fprintf(fp, "LOOKUP_TABLE default\n");
        if (numParticlesToWrite == numParticles) {
            for (pluint iParticle = 0; iParticle < numParticles; iParticle++) {
                float tag = (float) found[iParticle]->getTag();
                fprintf(fp, "% .7e\n", tag);
            }
        } else {
            for (pluint i = 0; i < numParticlesToWrite; i++) {
                float tag = (float) found[iParticles[i]]->getTag();
                fprintf(fp, "% .7e\n", tag);
            }
        }
        std::map<plint,std::string>::const_iterator scalarIt = additionalScalars.begin();
        for (; scalarIt != additionalScalars.end(); ++scalarIt) {
            plint scalarID = scalarIt->first;
            std::string scalarName = scalarIt->second;
            fprintf(fp, "SCALARS ");
            fprintf(fp, "%s", scalarName.c_str());
            fprintf(fp, " float\n");
            fprintf(fp, "LOOKUP_TABLE default\n");
            if (numParticlesToWrite == numParticles) {
                for (pluint iParticle = 0; iParticle < numParticles; iParticle++) {
                    T scalarValue;
                    found[iParticle]->getScalar(scalarID, scalarValue);
                    float floatScalarValue = (float) scalarValue;
                    fprintf(fp, "% .7e\n", floatScalarValue);
                }
            } else {
                for (pluint i = 0; i < numParticlesToWrite; i++) {
                    T scalarValue;
                    found[iParticles[i]]->getScalar(scalarID, scalarValue);
                    float floatScalarValue = (float) scalarValue;
                    fprintf(fp, "% .7e\n", floatScalarValue);
                }
            }
        }
        fclose(fp);
    }
}

template<typename T, template<typename U> class Descriptor>
void writeParticleVtk (
        MultiParticleField3D<DenseParticleField3D<T,Descriptor> >& particles,
        std::string const& fName, pluint maxNumParticlesToWrite )
{
    std::map<plint,std::string> additionalScalars;
    std::map<plint,std::string> additionalVectors;
    additionalVectors[0] = "Velocity";
    writeParticleVtk(particles, fName, additionalScalars, additionalVectors, maxNumParticlesToWrite);
}

template<typename T, template<typename U> class Descriptor>
void writeParticleVtk (
        MultiParticleField3D<DenseParticleField3D<T,Descriptor> >& particles,
        std::string const& fName,
        std::map<plint,std::string> const& additionalScalars,
        std::map<plint,std::string> const& additionalVectors,
        pluint maxNumParticlesToWrite )
{
    SparseBlockStructure3D blockStructure(particles.getBoundingBox());
    blockStructure.addBlock(particles.getBoundingBox(), 0);
    plint envelopeWidth=1;
    MultiBlockManagement3D serialMultiBlockManagement (
            blockStructure, new OneToOneThreadAttribution, envelopeWidth );

    MultiParticleField3D<DenseParticleField3D<T,Descriptor> > multiSerialParticles (
            serialMultiBlockManagement,
            defaultMultiBlockPolicy3D().getCombinedStatistics() );

    copy ( particles, particles.getBoundingBox(), multiSerialParticles, particles.getBoundingBox() );
    particleVtkSerialImplementation(multiSerialParticles, fName,
                                    additionalScalars, additionalVectors, maxNumParticlesToWrite);
}

template<typename T, template<typename U> class Descriptor>
void writeSelectedParticleVtk (
        MultiParticleField3D<DenseParticleField3D<T,Descriptor> >& particles,
        std::string const& fName, Box3D const& domain, util::SelectInt const& tags )
{
    std::map<plint,std::string> additionalScalars;
    std::map<plint,std::string> additionalVectors;
    additionalVectors[0] = "Velocity";
    writeSelectedParticleVtk(particles, fName, domain, tags, additionalScalars, additionalVectors);
}

template<typename T, template<typename U> class Descriptor>
void writeSelectedParticleVtk (
        MultiParticleField3D<DenseParticleField3D<T,Descriptor> >& particles,
        std::string const& fName, Box3D const& domain, util::SelectInt const& tags,
        std::map<plint,std::string> const& additionalScalars,
        std::map<plint,std::string> const& additionalVectors )
{
    MultiParticleField3D<DenseParticleField3D<T,Descriptor> > filteredParticles((MultiBlock3D&)particles);
    std::vector<MultiBlock3D*> particleArgs;
    particleArgs.push_back(&particles);
    particleArgs.push_back(&filteredParticles);
    applyProcessingFunctional (
            new CopySelectParticles3D<T,Descriptor>(tags.clone()),
            domain, particleArgs );
    SparseBlockStructure3D blockStructure(particles.getBoundingBox());
    blockStructure.addBlock(particles.getBoundingBox(), 0);
    plint envelopeWidth=1;
    MultiBlockManagement3D serialMultiBlockManagement (
            blockStructure, new OneToOneThreadAttribution, envelopeWidth );

    MultiParticleField3D<DenseParticleField3D<T,Descriptor> > multiSerialParticles (
            serialMultiBlockManagement,
            defaultMultiBlockPolicy3D().getCombinedStatistics() );

    copy ( filteredParticles, domain, multiSerialParticles, domain );
    particleVtkSerialImplementation(multiSerialParticles, fName, additionalScalars, additionalVectors, 0);
}

}  // namespace plb

#endif  // PARTICLE_VTK_3D_HH