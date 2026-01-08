#!/usr/bin/env python
# coding: utf-8

"""
Movies Project - Spark (RDD APIs)

Implements:
Q1 [Required] From 1995 onwards, for each year, find the movies with the Comedy genre.
Q2 [Required] Percentage of users who have given rating 5 to at least a movie.
Q3 [Required] Percentage of users whose average rating >= 3.5.
Q4 [Optional] For each genre, find the average rating of the genre and number of films.

Run this script with spark-submit from the folder containing movies.csv and ratings.csv.
"""

from pyspark import SparkContext
from io import StringIO
import csv


def split_complex(line: str):
    """Parse a CSV line correctly (handles commas inside quotes)."""
    return next(csv.reader(StringIO(line), delimiter=","))


def split_genra(genres: str):
    """Split pipe-separated genres into a list (assignment spelling kept)."""
    if not genres:
        return []
    return next(csv.reader(StringIO(genres), delimiter="|"))


def get_year(title: str):
    """Extract year from 'Title (1995)' -> '1995'."""
    if "(" not in title or ")" not in title:
        return None
    try:
        return title.split("(")[-1].split(")")[0]
    except Exception:
        return None


def get_title(title: str):
    """Extract title from 'Title (1995)' -> 'Title'."""
    if "(" not in title:
        return title.strip()
    return title.split("(")[0].strip()


def pct(numer, denom):
    return (numer / denom * 100.0) if denom else 0.0


if __name__ == "__main__":
    # Match assignment style: SparkContext("local", "test")
    sc = SparkContext("local", "test")
    sc.setLogLevel("ERROR")

    # -------------------------
    # Q1
    # From 1995 onwards, for each year, find the movie with the Comedy genre.
    # (The assignment sample code actually returns all Comedy movie titles per year.)
    # -------------------------
    moviesRDD = sc.textFile("movies.csv")
    header = moviesRDD.first()
    moviesRDD1 = moviesRDD.filter(lambda line: line != header)

    # Parse once per line (more efficient than calling split_complex multiple times)
    # Output: (year:int, title:str, movieId:str, genres:list[str])
    moviesRDD2 = (
        moviesRDD1
        .map(split_complex)
        .map(lambda r: (r[0], r[1], r[2]))  # (movieId, titleWithYear, genresStr)
        .map(lambda x: (get_year(x[1]), get_title(x[1]), x[0], split_genra(x[2])))
        .filter(lambda x: x[0] is not None)
        .map(lambda x: (int(x[0]), x[1], x[2], x[3]))
    )

    moviesAfter1995RDD = moviesRDD2.filter(lambda x: x[0] >= 1995)

    # Equivalent to the assignment code, but fewer shuffles:
    comedyMoviesAfter1995 = (
        moviesAfter1995RDD
        .flatMap(lambda x: [(x[0], x[1])] if "Comedy" in x[3] else [])
    )

    # groupByKey() works but can be memory-heavy; combineByKey is safer for scale.
    Q1ResultRDD = (
        comedyMoviesAfter1995
        .combineByKey(
            lambda v: [v],
            lambda acc, v: (acc.append(v) or acc),
            lambda a, b: a + b
        )
        .mapValues(lambda lst: sorted(lst))
        .sortByKey()
    )

    # -------------------------
    # Q2
    # Percentage of users who have given rating 5 to at least a movie.
    # -------------------------
    ratingsRDD = sc.textFile("ratings.csv")
    header = ratingsRDD.first()
    ratingsRDD1 = ratingsRDD.filter(lambda line: line != header)

    ratingsParsed = (
        ratingsRDD1
        .map(split_complex)
        .map(lambda r: (r[0], r[1], float(r[2]), r[3]))  # (userId, movieId, rating, timestamp)
        .cache()
    )

    q2UsersGood = (
        ratingsParsed
        .filter(lambda x: x[2] == 5.0)
        .map(lambda x: x[0])
        .distinct()
        .count()
    )

    q2UsersTotal = ratingsParsed.map(lambda x: x[0]).distinct().count()

    # -------------------------
    # Q3
    # Percentage of users whose average rating is >= 3.5.
    # -------------------------
    userAvgRatesRDD = (
        ratingsParsed
        .map(lambda x: (x[0], x[2]))               # (userId, rating)
        .mapValues(lambda r: (r, 1))               # (userId, (sum, count))
        .reduceByKey(lambda a, b: (a[0] + b[0], a[1] + b[1]))
        .mapValues(lambda s: s[0] / s[1])          # (userId, avg)
        .cache()
    )

    q3UsersGood = userAvgRatesRDD.filter(lambda x: x[1] >= 3.5).count()
    q3UsersTotal = userAvgRatesRDD.count()

    # -------------------------
    # Q4 (Optional)
    # For each genre: (avg rating of the genre, number of films in the genre).
    # -------------------------
    movieIdToGenres = moviesRDD2.map(lambda x: (x[2], x[3]))  # (movieId, [genres])

    movieIdToRatingStats = (
        ratingsParsed
        .map(lambda x: (x[1], x[2]))                 # (movieId, rating)
        .mapValues(lambda r: (r, 1))
        .reduceByKey(lambda a, b: (a[0] + b[0], a[1] + b[1]))
    )

    movieIdToAvg = movieIdToRatingStats.mapValues(lambda s: s[0] / s[1])

    movieGenresAndAvg = movieIdToGenres.join(movieIdToAvg)

    genreToMovieAvg = movieGenresAndAvg.flatMap(
        lambda x: [(g, (x[1][1], 1)) for g in x[1][0]]
    )

    genreTotals = genreToMovieAvg.reduceByKey(lambda a, b: (a[0] + b[0], a[1] + b[1]))

    Q4ResultRDD = genreTotals.mapValues(lambda x: (x[0] / x[1], x[1])).sortByKey()

    # -------------------------
    # Print outputs (screenshot-friendly)
    # -------------------------
    print("\nQ1 Output (first 5 years shown):")
    print(Q1ResultRDD.take(5))

    print("\nQ2 Output (percentage):")
    print(f"{pct(q2UsersGood, q2UsersTotal):.2f}%")

    print("\nQ3 Output (percentage):")
    print(f"{pct(q3UsersGood, q3UsersTotal):.2f}%")

    print("\nQ4 Output (first 10) [Optional]:")
    print(Q4ResultRDD.take(10))

    print("\n========== END ==========")

    sc.stop()
