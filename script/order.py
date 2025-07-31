import pandas as pd
import numpy as np
from collections import defaultdict, Counter

# 1. 准备数据
data = {
    'clock': [1, 2, 3, 4, 5, 6, 7, 8],
    'stock_id': [1, 2, 3, 4, 3, 4, 1, 2],
    'time': ['9:00:00', '9:00:00', '9:00:00', '9:00:00', 
             '9:00:03', '9:00:03', '9:00:03', '9:00:03'],
    'price': [100, 101, 102, 103, 104, 105, 106, 107]
}

df = pd.DataFrame(data)

# 2. 按照时间分组，并在每个组内按clock排序
groups = df.groupby('time')
ordered_groups = []

for name, group in groups:
    # 按clock排序，确保顺序正确
    ordered_group = group.sort_values('clock').reset_index(drop=True)
    ordered_groups.append(ordered_group)

# 3. 统计每个stock_id前面出现的stock_id
preceding_counts = defaultdict(Counter)

for group in ordered_groups:
    # 遍历每个组中的股票，记录前一个出现的股票
    for i in range(1, len(group)):
        current_stock = group.loc[i, 'stock_id']
        prev_stock = group.loc[i-1, 'stock_id']
        preceding_counts[current_stock][prev_stock] += 1

# 4. 确定每个stock_id前面出现最多次的stock_id
most_common_preceding = {}
for stock, counts in preceding_counts.items():
    if counts:  # 确保有数据
        most_common = counts.most_common(1)[0][0]
        most_common_preceding[stock] = most_common

print("每个股票ID前出现最多次的股票ID:")
for stock, prev_stock in most_common_preceding.items():
    print(f"股票 {stock} 前面最常出现的是股票 {prev_stock}")

# 5. 构建反向映射：从前面的股票到后面的股票
next_stocks = defaultdict(list)
for stock, prev_stock in most_common_preceding.items():
    next_stocks[prev_stock].append(stock)

# 6. 从指定起始点构建4个最终分组
start_points = [1, 2, 5, 9]
final_groups = {start: [start] for start in start_points}

# 遍历所有股票，根据最常见的前置关系分配到相应的组
all_stocks = set(df['stock_id'].unique())

# 处理数据中存在的起始点
existing_starts = [s for s in start_points if s in all_stocks]
# 处理数据中不存在的起始点
non_existing_starts = [s for s in start_points if s not in all_stocks]

# 对于存在的起始点，使用它们的后续关系
for start in existing_starts:
    current = start
    while True:
        # 找到当前股票的下一个可能股票
        next_possible = next_stocks.get(current, [])
        # 选择第一个未被分配的股票
        assigned = False
        for stock in next_possible:
            if stock not in [s for group in final_groups.values() for s in group]:
                final_groups[start].append(stock)
                current = stock
                assigned = True
                break
        if not assigned:
            break

# 对于未分配的股票，平均分配到各个组
assigned_stocks = set([s for group in final_groups.values() for s in group])
unassigned_stocks = all_stocks - assigned_stocks - set(start_points)

# 平均分配未分配的股票
for i, stock in enumerate(unassigned_stocks):
    group_index = i % len(start_points)
    final_groups[start_points[group_index]].append(stock)

# 7. 输出最终分组
print("\n最终分组结果:")
for i, (start, group) in enumerate(final_groups.items(), 1):
    print(f"组 {i} (起始股票 {start}): {sorted(group)}")
