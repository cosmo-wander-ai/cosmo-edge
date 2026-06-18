add_library(cosmo_db OBJECT
    ArticlesReidDao.cc   ArticlesReidDao.h
    ConditionBuilder.cc  ConditionBuilder.h
    DaoBase.cc           DaoBase.h
    FaceTaskEventDao.cc  FaceTaskEventDao.h
    PassengerFlowDao.cc  PassengerFlowDao.h
    PersonDao.cc         PersonDao.h
    PersonDaoQuery.cc                    # partition of PersonDao
    PersonRecogDao.cc    PersonRecogDao.h
    RowFieldReader.cc    RowFieldReader.h
    TaskEventDao.cc      TaskEventDao.h
)

# Project headers (db/ and util/ via src/)
target_include_directories(cosmo_db PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(cosmo_db PRIVATE cosmo_common SQLiteCpp sqlite3)
