#!/bin/bash

export NOMBUILDPATH=`pwd`/monnombuild 
cd sourcecode/BenchmarkGenerator/
dotnet build -c Release BenchmarkGenerator.csproj
cd ../..
cd sourcecode/NomC
dotnet build -c Release NomC.csproj
cd ../..
cd sourcecode/NomProject
dotnet build -c Release NomProject.csproj
cd ../..
